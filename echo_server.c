#include "async_io.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>

void echo_client(void* arg) {
    int fd = *(int*)arg;
    free(arg);
    
    char buffer[1024];
    printf("Client connected\n");
    
    while (1) {
        future_t* f = async_read(fd, buffer, sizeof(buffer) - 1);
        int n = future_wait(f);
        
        if (n <= 0) break;
        
        buffer[n] = '\0';
        printf("Echo: %s", buffer);
        
        future_t* wf = async_write(fd, buffer, n);
        future_wait(wf);
        future_free(wf);
        future_free(f);
    }
    
    close(fd);
    printf("Client disconnected\n");
}

int main() {
    printf("Simple Echo Server\n");
    
    async_io_init();
    
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(8888);
    
    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 10);
    
    printf("Echo server on port 8888\n");
    printf("Test: telnet localhost 8888\n");
    
    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd > 0) {
            int* fd_ptr = malloc(sizeof(int));
            *fd_ptr = client_fd;
            
            pthread_t tid;
            pthread_create(&tid, NULL, (void*)echo_client, fd_ptr);
            pthread_detach(tid);
        }
    }
    
    return 0;
}
