#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 9999
#define BUFFER_SIZE 1024

// Simple thread pool for handling clients
typedef struct {
    int client_fd;
    pthread_t thread;
} client_handler_t;

void* handle_client(void* arg) {
    int client_fd = *(int*)arg;
    free(arg);
    
    char buffer[BUFFER_SIZE];
    char response[] = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello World!\r\n";
    
    printf("Client %d connected\n", client_fd);
    
    // Read request (non-blocking simulation)
    ssize_t n = read(client_fd, buffer, BUFFER_SIZE - 1);
    if (n > 0) {
        buffer[n] = '\0';
        printf("Received: %s\n", buffer);
        
        // Send response
        write(client_fd, response, strlen(response));
    }
    
    close(client_fd);
    printf("Client %d disconnected\n", client_fd);
    return NULL;
}

int main() {
    printf("Simple Thread-Per-Client Server\n");
    printf("================================\n\n");
    
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        return 1;
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("bind");
        return 1;
    }
    
    if (listen(server_fd, 128) == -1) {
        perror("listen");
        return 1;
    }
    
    printf("Server listening on port %d\n", PORT);
    printf("Test: curl http://localhost:%d\n\n", PORT);
    
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_fd > 0) {
            printf("Connection from %s:%d\n", 
                   inet_ntoa(client_addr.sin_addr), 
                   ntohs(client_addr.sin_port));
            
            int* fd_ptr = malloc(sizeof(int));
            *fd_ptr = client_fd;
            
            pthread_t tid;
            pthread_create(&tid, NULL, handle_client, fd_ptr);
            pthread_detach(tid);
        }
    }
    
    close(server_fd);
    return 0;
}
