#include "golight.h"

#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SERVER_PORT 8888
#define MAX_CLIENTS 100
#define REQUEST_SIZE 4096

static atomic_int handled_clients;

static void handle_client(void* arg) {
    int client_fd = *(int*)arg;
    free(arg);

    char request[REQUEST_SIZE];
    future_t* read_future = async_read(client_fd, request, sizeof(request) - 1);
    if (!read_future) {
        close(client_fd);
        return;
    }

    int bytes_read = future_wait(read_future);
    future_free(read_future);
    if (bytes_read <= 0) {
        close(client_fd);
        return;
    }

    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 11\r\n"
        "Connection: close\r\n"
        "\r\n"
        "Hello World";
    size_t sent = 0;
    while (sent < sizeof(response) - 1) {
        future_t* write_future =
            async_write(client_fd, response + sent, sizeof(response) - 1 - sent);
        if (!write_future) {
            close(client_fd);
            return;
        }

        int bytes_written = future_wait(write_future);
        future_free(write_future);
        if (bytes_written <= 0) {
            close(client_fd);
            return;
        }
        sent += (size_t)bytes_written;
    }

    int completed = atomic_fetch_add(&handled_clients, 1) + 1;
    printf("Handled client %d (goroutine %llu)\n", completed,
           (unsigned long long)goroutine_id());
    close(client_fd);
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
        perror("setsockopt");
        close(server_fd);
        return EXIT_FAILURE;
    }

    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(SERVER_PORT),
    };
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) == -1) {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }
    if (listen(server_fd, MAX_CLIENTS) == -1) {
        perror("listen");
        close(server_fd);
        return EXIT_FAILURE;
    }

    if (runtime_init() != 0) {
        perror("runtime_init");
        close(server_fd);
        return EXIT_FAILURE;
    }
    printf("GoLight echo server listening on port %d (limit %d clients)\n",
           SERVER_PORT, MAX_CLIENTS);

    int accepted = 0;
    while (accepted < MAX_CLIENTS) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd == -1) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }

        int* client_arg = malloc(sizeof(*client_arg));
        if (!client_arg) {
            perror("malloc");
            close(client_fd);
            continue;
        }
        *client_arg = client_fd;
        if (go(handle_client, client_arg) != 0) {
            perror("go");
            close(client_fd);
            free(client_arg);
            break;
        }
        accepted++;
    }

    close(server_fd);
    runtime_wait();
    int completed = atomic_load(&handled_clients);
    printf("Server handled %d/%d clients\n", completed, accepted);
    runtime_shutdown();

    return completed == accepted && accepted == MAX_CLIENTS
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
