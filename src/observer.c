#include "../include/common.h"

#define C_RESET   "\033[0m"
#define C_BOLD    "\033[1m"
#define C_GREEN   "\033[1;32m"
#define C_YELLOW  "\033[1;33m"
#define C_RED     "\033[1;31m"
#define C_CYAN    "\033[1;36m"
#define C_WHITE   "\033[1;37m"

// Desenha visualmente o anel lógico P0 -> P1 -> P2 -> P3 -> P4 -> P0
static void print_visual_ring(const int node_in_cs[NUM_NODES], int active_count) {
    printf("   Anel: ");
    for (int i = 0; i < NUM_NODES; i++) {
        if (node_in_cs[i]) {
            if (active_count > 1) {
                printf(C_RED "[ COLISAO P%d ]" C_RESET, i);
            } else {
                printf(C_GREEN "[ P%d USANDO R ]" C_RESET, i);
            }
        } else {
            printf("( P%d )", i);
        }
        printf(" --> ");
    }
    printf("(P0)\n");
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);

    int server_fd = create_server_socket(OBSERVER_PORT);
    int active_in_cs = 0;
    int node_in_cs[NUM_NODES] = {0};
    int total_violations = 0;
    int total_accesses = 0;
    int accesses_per_client[NUM_NODES] = {0};

    int nodes_finished = 0;
    double node_medians[NUM_NODES] = {0};
    long node_token_msgs[NUM_NODES] = {0};

    double exp_start_ms = 0.0;
    double exp_end_ms = 0.0;

    char ts[32];
    get_WallClock(ts, sizeof(ts));
    printf(C_CYAN "================================================================================\n");
    printf(" [%s] OBSERVADOR INICIADO - MONITORANDO RECURSO 'R' EM TEMPO REAL\n", ts);
    printf("================================================================================\n" C_RESET);

    while (nodes_finished < NUM_NODES) {
        int client_sock = accept(server_fd, NULL, NULL);
        if (client_sock < 0) continue;

        Message msg;
        ssize_t n = recv(client_sock, &msg, sizeof(Message), MSG_WAITALL);
        if (n == sizeof(Message)) {
            get_WallClock(ts, sizeof(ts));

            if (msg.type == MSG_BEGIN) {
                if (exp_start_ms == 0.0) exp_start_ms = get_monotonic_ms();

                active_in_cs++;
                if (msg.node_id >= 0 && msg.node_id < NUM_NODES) {
                    node_in_cs[msg.node_id] = 1;
                }

                if (active_in_cs > 1) {
                    total_violations++;
                    printf("\n" C_RED "🚨 [%s] [VIOLACAO DETECTADA!] No %d entrou em R com recurso ja ocupado! (Simultaneos=%d)" C_RESET "\n",
                           ts, msg.node_id, active_in_cs);
                    print_visual_ring(node_in_cs, active_in_cs);
                } else {
                    printf("\n" C_GREEN "🔒 [%s] [BEGIN] No %d ENTROU na Secao Critica | Pedido: Cliente %d (#%d) | Volta do Token: %ld | Ativos em R: %d" C_RESET "\n",
                           ts, msg.node_id, msg.client_id, msg.req_seq, msg.token_seq, active_in_cs);
                    print_visual_ring(node_in_cs, active_in_cs);
                }

                Message ack = { .type = MSG_ACK };
                send(client_sock, &ack, sizeof(Message), 0);

            } else if (msg.type == MSG_END) {
                active_in_cs--;
                if (msg.node_id >= 0 && msg.node_id < NUM_NODES) {
                    node_in_cs[msg.node_id] = 0;
                }
                total_accesses++;
                if (msg.client_id >= 0 && msg.client_id < NUM_NODES) {
                    accesses_per_client[msg.client_id]++;
                }

                int next_node = (msg.node_id + 1) % NUM_NODES;
                printf(C_YELLOW "🔓 [%s] [END]   No %d SAIU da Secao Critica   | Pedido: Cliente %d (#%d) | Passando Token -> No %d | Ativos em R: %d" C_RESET "\n",
                       ts, msg.node_id, msg.client_id, msg.req_seq, next_node, active_in_cs);

                Message ack = { .type = MSG_ACK };
                send(client_sock, &ack, sizeof(Message), 0);
                exp_end_ms = get_monotonic_ms();

            } else if (msg.type == MSG_NODE_SUMMARY) {
                node_medians[msg.node_id] = msg.median_wait_ms;
                node_token_msgs[msg.node_id] = msg.token_msgs_sent;
                nodes_finished++;
                printf(C_CYAN "ℹ️  [%s] [STATUS] Gerenciador No %d finalizou todos os pedidos do seu cliente (%d/%d concluidos)\n" C_RESET,
                       ts, msg.node_id, nodes_finished, NUM_NODES);
            }
        }
        close(client_sock);
    }

    long total_token_msgs = 0;
    for (int i = 0; i < NUM_NODES; i++) total_token_msgs += node_token_msgs[i];
    double total_time_s = (exp_end_ms - exp_start_ms) / 1000.0;

    printf("\n" C_CYAN "╔══════════════════════════════════════════════════════════════════════════════╗\n");
    printf("║                 RELATÓRIO FINAL DO OBSERVADOR / VERIFICADOR                  ║\n");
    printf("╠══════════════════════════════════════════════════════════════════════════════╣\n" C_RESET);
    if (total_violations == 0) {
        printf("║ Status da Exclusão Mútua : " C_GREEN "✅ APROVADO (0 colisões / sobreposições em R)" C_RESET "     ║\n");
    } else {
        printf("║ Status da Exclusão Mútua : " C_RED "❌ REPROVADO (%3d colisões detectadas em R!)" C_RESET "      ║\n", total_violations);
    }
    printf("║ Total de Acessos a 'R'   : %-49d ║\n", total_accesses);
    printf("║ Tempo Total de Execução  : %-6.2f segundos                                   ║\n", total_time_s);
    printf("║ Giros do Token (Rede)    : %-8ld (Média: %6.2f mensagens/acesso)         ║\n",
           total_token_msgs, total_accesses > 0 ? (double)total_token_msgs / total_accesses : 0.0);
    printf("║ Msgs de Instrumentação   : %-8d (BEGIN + END enviados ao Observador)     ║\n", total_accesses * 2);
    printf(C_CYAN "╠════════════╦══════════════════╦═══════════════════════╦══════════════════════╣\n");
    printf("║ Nó / Par   ║ Acessos Feitos   ║ Espera Mediana (ms)   ║ Tokens Repassados    ║\n");
    printf("╠════════════╬══════════════════╬═══════════════════════╬══════════════════════╣\n" C_RESET);
    for (int i = 0; i < NUM_NODES; i++) {
        printf("║ P%d (Cli %d) ║ %8d acessos ║ %14.2f ms     ║ %13ld msgs   ║\n",
               i, i, accesses_per_client[i], node_medians[i], node_token_msgs[i]);
    }
    printf(C_CYAN "╚════════════╩══════════════════╩═══════════════════════╩══════════════════════╝\n" C_RESET);

    close(server_fd);
    return (total_violations == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}