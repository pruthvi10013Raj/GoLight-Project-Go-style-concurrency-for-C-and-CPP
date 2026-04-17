#include "worker_stealing.h"
#include "async_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUFFER_SIZE 4096

void handle_client(void* arg) {
    int client_fd = *(int*)arg;
    free(arg);
    
    char buffer[BUFFER_SIZE];
    char response[] = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello World!\r\n";
    
    printf("Client connected on fd=%d\n", client_fd);
    
    while (1) {
        future_t* read_f = async_read(client_fd, buffer, BUFFER_SIZE - 1);
        int n = future_wait(read_f);
        future_free(read_f);
        
        if (n <= 0) break;
        
        buffer[n] = '\0';
        printf("Received: %s\n", buffer);
        
        future_t* write_f = async_write(client_fd, response, strlen(response));
        future_wait(write_f);
        future_free(write_f);
    }
    
    close(client_fd);
    printf("Client disconnected\n");
}

void server_main(void* arg) {
    int port = *(int*)arg;
    free(arg);
    
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        return;
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("bind");
        close(server_fd);
        return;
    }
    
    if (listen(server_fd, 128) == -1) {
        perror("listen");
        close(server_fd);
        return;
    }
    
    printf("Server listening on port %d\n", port);
    
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd == -1) {
            perror("accept");
            continue;
        }
        
        printf("New connection from %s\n", inet_ntoa(client_addr.sin_addr));
        
        int* fd_ptr = malloc(sizeof(int));
        *fd_ptr = client_fd;
        go(handle_client, fd_ptr);
    }
    
    close(server_fd);
}

int main() {
    printf("╔════════════════════════════════════════════╗\n");
    printf("║     Async I/O Network Server              ║\n");
    printf("╚════════════════════════════════════════════╝\n\n");
    
    // Initialize runtime
    runtime_init();
    
    // Initialize async I/O
    if (async_io_init() != 0) {
        fprintf(stderr, "Failed to initialize async I/O\n");
        return 1;
    }
    
    // Start server
    int* port = malloc(sizeof(int));
    *port = 8080;
    go(server_main, port);
    
    printf("Server running. Connect with: curl http://localhost:8080\n");
    printf("Press Ctrl+C to stop\n");
    
    // Wait indefinitely
    runtime_wait();
    
    async_io_shutdown();
    runtime_shutdown();
    
    return 0;
}
