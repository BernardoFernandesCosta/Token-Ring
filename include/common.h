#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#define NUM_NODES 5
#define OBSERVER_PORT 7000
#define NODE_RING_PORT 5000
#define NODE_CLIENT_PORT 6000

typedef enum {
    MSG_REQUEST = 1,
    MSG_COMMITTED,
    MSG_TOKEN,
    MSG_BEGIN,
    MSG_END,
    MSG_ACK,
    MSG_NODE_SUMMARY
} MsgType;

typedef struct {
    MsgType type;
    int node_id;
    int client_id;
    int req_seq;          // Compõe o request_id único: (client_id, req_seq)
    long token_seq;       // Posição/sequência global do token no anel
    int cs_duration_ms;   // Duração sorteada para a seção crítica
    // Campos para o relatório final de métricas
    int completed_reqs;
    double median_wait_ms;
    long token_msgs_sent;
} Message;

// Relógio monotônico em milissegundos para medição local de espera
static inline double get_monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000.0) + (ts.tv_nsec / 1000000.0);
}

// Timestamp físico apenas para exibição visual nos logs
static inline void get_WallClock(char *buf, size_t len) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm_info;
    localtime_r(&ts.tv_sec, &tm_info);
    snprintf(buf, len, "%02d:%02d:%02d.%03ld",
             tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec, ts.tv_nsec / 1000000);
}

// Abre um socket TCP servidor pronto para escutar conexões
static inline int create_server_socket(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 32) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    return server_fd;
}

// Conecta a um host/container pelo nome DNS do Docker com retries
static inline int connect_to_host(const char *host, int port, int max_retries) {
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    for (int attempt = 0; attempt < max_retries; attempt++) {
        struct addrinfo hints = {0}, *res = NULL;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        if (getaddrinfo(host, port_str, &hints, &res) == 0) {
            int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
            if (sock >= 0) {
                if (connect(sock, res->ai_addr, res->ai_addrlen) == 0) {
                    freeaddrinfo(res);
                    return sock;
                }
                close(sock);
            }
            freeaddrinfo(res);
        }
        usleep(100000); // Aguarda 100ms antes de tentar novamente
    }
    return -1;
}

#endif