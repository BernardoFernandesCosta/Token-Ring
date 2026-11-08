#include "../include/common.h"
#include <signal.h>

#define C_RESET   "\033[0m"
#define C_BLUE    "\033[1;34m"
#define C_MAGENTA "\033[1;35m"
#define C_CYAN    "\033[1;36m"

static int node_id;
static char next_host[64];
static char observer_host[64];
static int sync_enabled = 1;
static int verbose_logs = 1;
static unsigned int rng_seed;

static pthread_mutex_t internal_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv_token_arrived = PTHREAD_COND_INITIALIZER;

static int wants_to_enter = 0;
static int has_token = 0;
static long active_token_seq = 0;
static long token_msgs_sent = 0;
static volatile sig_atomic_t simulation_running = 1;

// Permite que o Docker encerre o contêiner imediatamente ao final do experimento
static void handle_sigterm(int sig) {
    (void)sig;
    _exit(0);
}

static int cmp_double(const void *a, const void *b) {
    double da = *(const double*)a;
    double db = *(const double*)b;
    return (da > db) - (da < db);
}

static void notify_observer(MsgType type, int client_id, int req_seq, long token_seq) {
    int sock = connect_to_host(observer_host, OBSERVER_PORT, 30);
    if (sock < 0) return;

    Message msg = {
        .type = type,
        .node_id = node_id,
        .client_id = client_id,
        .req_seq = req_seq,
        .token_seq = token_seq
    };
    send(sock, &msg, sizeof(Message), 0);

    Message ack;
    recv(sock, &ack, sizeof(Message), MSG_WAITALL);
    close(sock);
}

static void forward_token_to_next(long next_seq) {
    int sock = connect_to_host(next_host, NODE_RING_PORT, 50);
    if (sock >= 0) {
        Message tok = {
            .type = MSG_TOKEN,
            .node_id = node_id,
            .token_seq = next_seq
        };
        send(sock, &tok, sizeof(Message), 0);
        close(sock);
        pthread_mutex_lock(&internal_lock);
        token_msgs_sent++;
        pthread_mutex_unlock(&internal_lock);
    }
}

// =========================================================================
// 1. ACQUIRE: Solicita entrada e aguarda a chegada do Token no anel
// =========================================================================
void acquire(const char *resource, int client_id, int req_seq, double *wait_ms_out) {
    char ts[32];
    if (verbose_logs) {
        get_WallClock(ts, sizeof(ts));
        printf(C_BLUE "📩 [%s] [NO %d] [1. REQUEST] Recebeu pedido do Cliente %d (#%d) -> Aguardando Token..." C_RESET "\n",
               ts, node_id, client_id, req_seq);
    }

    double t_start = get_monotonic_ms();

    if (sync_enabled) {
        pthread_mutex_lock(&internal_lock);
        wants_to_enter = 1;
        while (!has_token) {
            pthread_cond_wait(&cv_token_arrived, &internal_lock);
        }
        pthread_mutex_unlock(&internal_lock);
    }

    *wait_ms_out = get_monotonic_ms() - t_start;
    if (verbose_logs) {
        get_WallClock(ts, sizeof(ts));
        printf(C_MAGENTA "🔑 [%s] [NO %d] [2. ACQUIRE] Pegou o Token (Seq=%ld) para %s | Esperou %.2f ms na fila" C_RESET "\n",
               ts, node_id, active_token_seq, resource, *wait_ms_out);
    }
}

// =========================================================================
// 2. RELEASE: Libera o estado local e despacha o Token para o próximo nó
// =========================================================================
void release(const char *resource, int client_id, int req_seq) {
    char ts[32];
    if (verbose_logs) {
        get_WallClock(ts, sizeof(ts));
        printf(C_CYAN "➡️  [%s] [NO %d] [5. RELEASE] Liberou %s (Req C%d-S%d) e repassou o Token adiante" C_RESET "\n",
               ts, node_id, resource, client_id, req_seq);
    }

    if (sync_enabled) {
        pthread_mutex_lock(&internal_lock);
        wants_to_enter = 0;
        has_token = 0;
        long next_seq = active_token_seq + 1;
        pthread_mutex_unlock(&internal_lock);

        // Repassa o token diretamente ao vizinho antes de atender o próximo pedido local
        forward_token_to_next(next_seq);
    }
}

// =========================================================================
// SEÇÃO CRÍTICA SIMULADA
// =========================================================================
void execute_critical_section(int client_id, int req_seq) {
    char ts[32];
    int sleep_ms = 200 + (rand_r(&rng_seed) % 801);

    if (verbose_logs) {
        get_WallClock(ts, sizeof(ts));
        printf("⏳ [%s] [NO %d] [3. ENTER]   Escrevendo no recurso R por %d ms (Req C%d-S%d)...\n",
               ts, node_id, sleep_ms, client_id, req_seq);
    }

    notify_observer(MSG_BEGIN, client_id, req_seq, active_token_seq);
    usleep(sleep_ms * 1000);
    notify_observer(MSG_END, client_id, req_seq, active_token_seq);

    if (verbose_logs) {
        get_WallClock(ts, sizeof(ts));
        printf("🏁 [%s] [NO %d] [4. EXIT]    Terminou escrita de %d ms em R (Req C%d-S%d)\n",
               ts, node_id, sleep_ms, client_id, req_seq);
    }
}

// =========================================================================
// THREAD DO ANEL: Recebe o Token do nó anterior
// =========================================================================
void* ring_listener_thread(void *arg) {
    (void)arg;
    int ring_fd = create_server_socket(NODE_RING_PORT);

    if (node_id == 0 && sync_enabled) {
        sleep(2);
        char ts[32];
        get_WallClock(ts, sizeof(ts));
        printf(C_MAGENTA "🚀 [%s] [NO 0] Injetando o TOKEN UNICO inicial no anel (P0 -> P1 -> P2 -> P3 -> P4 -> P0)\n" C_RESET, ts);
        forward_token_to_next(1);
    }

    while (simulation_running) {
        int prev_sock = accept(ring_fd, NULL, NULL);
        if (prev_sock < 0) continue;

        Message msg;
        ssize_t n = recv(prev_sock, &msg, sizeof(Message), MSG_WAITALL);
        close(prev_sock);

        if (n == sizeof(Message) && msg.type == MSG_TOKEN) {
            pthread_mutex_lock(&internal_lock);
            active_token_seq = msg.token_seq;

            if (wants_to_enter && !has_token) {
                // Retém o token e acorda a thread principal bloqueada em acquire()
                has_token = 1;
                pthread_cond_signal(&cv_token_arrived);
                pthread_mutex_unlock(&internal_lock);
            } else {
                // Não há pedido local pendente: repassa o token para o próximo nó
                pthread_mutex_unlock(&internal_lock);
                usleep(10000);
                if (simulation_running) {
                    forward_token_to_next(msg.token_seq + 1);
                }
            }
        }
    }
    close(ring_fd);
    return NULL;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    signal(SIGTERM, handle_sigterm);
    signal(SIGINT, handle_sigterm);

    node_id = getenv("NODE_ID") ? atoi(getenv("NODE_ID")) : 0;
    snprintf(next_host, sizeof(next_host), "%s", getenv("NEXT_HOST") ? getenv("NEXT_HOST") : "127.0.0.1");
    snprintf(observer_host, sizeof(observer_host), "%s", getenv("OBSERVER_HOST") ? getenv("OBSERVER_HOST") : "127.0.0.1");
    sync_enabled = getenv("SYNC_ENABLED") ? atoi(getenv("SYNC_ENABLED")) : 1;
    verbose_logs = getenv("VERBOSE_LOGS") ? atoi(getenv("VERBOSE_LOGS")) : 1;
    rng_seed = (unsigned int)(time(NULL) ^ (node_id * 9973));

    pthread_t ring_tid;
    pthread_create(&ring_tid, NULL, ring_listener_thread, NULL);

    int client_server_fd = create_server_socket(NODE_CLIENT_PORT);
    double wait_times[200];
    int completed = 0;

    int client_sock = accept(client_server_fd, NULL, NULL);
    if (client_sock >= 0) {
        Message req;
        while (recv(client_sock, &req, sizeof(Message), MSG_WAITALL) == sizeof(Message)) {
            if (req.type == MSG_REQUEST) {
                double wait_ms = 0.0;

                acquire("R", req.client_id, req.req_seq, &wait_ms);
                execute_critical_section(req.client_id, req.req_seq);
                release("R", req.client_id, req.req_seq);

                if (completed < 200) wait_times[completed++] = wait_ms;

                Message resp = {
                    .type = MSG_COMMITTED,
                    .node_id = node_id,
                    .client_id = req.client_id,
                    .req_seq = req.req_seq
                };
                send(client_sock, &resp, sizeof(Message), 0);

                if (verbose_logs) {
                    char ts[32];
                    get_WallClock(ts, sizeof(ts));
                    printf("✅ [%s] [NO %d] [6. COMMITTED] Confirmado acesso #%d para o Cliente %d\n",
                           ts, node_id, req.req_seq, req.client_id);
                }
            }
        }
        close(client_sock);
    }

    double median_wait = 0.0;
    if (completed > 0) {
        qsort(wait_times, completed, sizeof(double), cmp_double);
        if (completed % 2 == 1) {
            median_wait = wait_times[completed / 2];
        } else {
            median_wait = (wait_times[(completed / 2) - 1] + wait_times[completed / 2]) / 2.0;
        }
    }

    int obs_sock = connect_to_host(observer_host, OBSERVER_PORT, 30);
    if (obs_sock >= 0) {
        pthread_mutex_lock(&internal_lock);
        long sent = token_msgs_sent;
        pthread_mutex_unlock(&internal_lock);

        Message summary = {
            .type = MSG_NODE_SUMMARY,
            .node_id = node_id,
            .completed_reqs = completed,
            .median_wait_ms = median_wait,
            .token_msgs_sent = sent
        };
        send(obs_sock, &summary, sizeof(Message), 0);
        close(obs_sock);
    }

    // Continua vivo repassando o token até o Observador finalizar o experimento
    while (simulation_running) {
        sleep(1);
    }
    close(client_server_fd);
    return 0;
}