#include "../include/common.h"

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);

    int client_id = getenv("CLIENT_ID") ? atoi(getenv("CLIENT_ID")) : 0;
    const char *target_host = getenv("TARGET_NODE_HOST") ? getenv("TARGET_NODE_HOST") : "127.0.0.1";
    int min_sleep_ms = getenv("MIN_SLEEP_MS") ? atoi(getenv("MIN_SLEEP_MS")) : 1000;
    int max_sleep_ms = getenv("MAX_SLEEP_MS") ? atoi(getenv("MAX_SLEEP_MS")) : 5000;
    int fixed_requests = getenv("NUM_REQUESTS") ? atoi(getenv("NUM_REQUESTS")) : 0;

    unsigned int seed = (unsigned int)(time(NULL) ^ (client_id * 7919));

    // Define entre 10 e 50 acessos caso NUM_REQUESTS não seja fixado
    int num_requests = (fixed_requests >= 10) ? fixed_requests : (10 + (rand_r(&seed) % 41));

    // Aguarda a inicialização do anel antes de iniciar a carga
    sleep(3);

    int sock = connect_to_host(target_host, NODE_CLIENT_PORT, 50);
    if (sock < 0) {
        fprintf(stderr, "[CLIENTE %d] Falha ao conectar ao gerenciador %s\n", client_id, target_host);
        return EXIT_FAILURE;
    }

    char ts[32];
    get_WallClock(ts, sizeof(ts));
    printf("[%s] [CLIENTE %d] Conectado a %s | Total de Pedidos=%d | Pausa=[%d..%d ms] | Seed=%u\n",
           ts, client_id, target_host, num_requests, min_sleep_ms, max_sleep_ms, seed);

    for (int seq = 1; seq <= num_requests; seq++) {
        Message req = {
            .type = MSG_REQUEST,
            .node_id = client_id,
            .client_id = client_id,
            .req_seq = seq
        };

        get_WallClock(ts, sizeof(ts));
        printf("[%s] [CLIENTE %d] Enviando REQUEST C%d-S%d (%d/%d)\n",
               ts, client_id, client_id, seq, seq, num_requests);

        if (send(sock, &req, sizeof(Message), 0) < 0) break;

        // Aguarda bloqueado pela confirmação COMMITTED
        Message resp;
        if (recv(sock, &resp, sizeof(Message), MSG_WAITALL) != sizeof(Message)) break;

        if (resp.type == MSG_COMMITTED) {
            get_WallClock(ts, sizeof(ts));
            printf("[%s] [CLIENTE %d] Recebeu COMMITTED para C%d-S%d\n",
                   ts, client_id, client_id, seq);
        }

        // Pausa entre pedidos (1 a 5s na Carga Normal; 0ms na Alta Contenção)
        if (seq < num_requests && max_sleep_ms > 0) {
            int range = max_sleep_ms - min_sleep_ms;
            int pause_ms = min_sleep_ms + (range > 0 ? (rand_r(&seed) % (range + 1)) : 0);
            usleep(pause_ms * 1000);
        }
    }

    get_WallClock(ts, sizeof(ts));
    printf("[%s] [CLIENTE %d] Finalizou todos os %d acessos.\n", ts, client_id, num_requests);
    close(sock);
    return EXIT_SUCCESS;
}