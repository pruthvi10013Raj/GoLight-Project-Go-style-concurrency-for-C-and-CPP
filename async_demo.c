#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_EVENTS 1024
#define BUFFER_SIZE 4096
#define PORT 8888

// Simple future structure
typedef struct future {
    int completed;
    int result;
    void* buffer;
    size_t size;
    pthread_cond_t cond;
    pthread_mutex_t lock;
} future_t;

// I/O operation
typedef struct io_op {
    int fd;
    int op;  // 0=read, 1=write
    future_t* future;
    void* buffer;
    size_t count;
} io_op_t;

// Global epoll instance
int epoll_fd;
pthread_t reactor_thread;
int reactor_running = 1;

// Set non-blocking
void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// Create and initialize future
future_t* future_create(void) {
    future_t* f = malloc(sizeof(future_t));
    f->completed = 0;
    f->result = 0;
    f->buffer = NULL;
    f->size = 0;
    pthread_cond_init(&f->cond, NULL);
    pthread_mutex_init(&f->lock, NULL);
    return f;
}

// Wait for future completion
int future_wait(future_t* f) {
    pthread_mutex_lock(&f->lock);
    while (!f->completed) {
        pthread_cond_wait(&f->cond, &f->lock);
    }
    pthread_mutex_unlock(&f->lock);
    return f->result;
}

// Signal future completion
void future_complete(future_t* f, int result) {
    pthread_mutex_lock(&f->lock);
    f->completed = 1;
    f->result = result;
    pthread_cond_signal(&f->cond);
    pthread_mutex_unlock(&f->lock);
}

// Async read
future_t* async_read(int fd, void* buf, size_t count) {
    future_t* f = future_create();
    f->buffer = buf;
    f->size = count;
    
    // Try immediate read
    ssize_t n = read(fd, buf, count);
    if (n >= 0) {
        future_complete(f, n);
        return f;
    }
    
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
        future_complete(f, -1);
        return f;
    }
    
    // Would block - register with epoll
    set_nonblocking(fd);
    
    io_op_t* op = malloc(sizeof(io_op_t));
    op->fd = fd;
    op->op = 0;  // read
    op->future = f;
    op->buffer = buf;
    op->count = count;
    
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLONESHOT;
    ev.data.ptr = op;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
    
    return f;
}

// Async write
future_t* async_write(int fd, const void* buf, size_t count) {
    future_t* f = future_create();
    
    // Try immediate write
    ssize_t n = write(fd, buf, count);
    if (n >= 0) {
        future_complete(f, n);
        return f;
    }
    
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
        future_complete(f, -1);
        return f;
    }
    
    // Would block - register with epoll
    set_nonblocking(fd);
    
    io_op_t* op = malloc(sizeof(io_op_t));
    op->fd = fd;
    op->op = 1;  // write
    op->future = f;
    op->buffer = (void*)buf;
    op->count = count;
    
    struct epoll_event ev;
    ev.events = EPOLLOUT | EPOLLONESHOT;
    ev.data.ptr = op;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
    
    return f;
}

// Reactor thread
void* reactor_loop(void* arg) {
    struct epoll_event events[MAX_EVENTS];
    
    printf("Reactor thread started\n");
    
    while (reactor_running) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 100);
        
        for (int i = 0; i < nfds; i++) {
            io_op_t* op = (io_op_t*)events[i].data.ptr;
            
            if (op->op == 0) {  // read
                ssize_t n = read(op->fd, op->buffer, op->count);
                future_complete(op->future, n);
            } else {  // write
                ssize_t n = write(op->fd, op->buffer, op->count);
                future_complete(op->future, n);
            }
            
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, op->fd, NULL);
            free(op);
        }
    }
    
    return NULL;
}

// Initialize async I/O
void async_init(void) {
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        exit(1);
    }
    
    pthread_create(&reactor_thread, NULL, reactor_loop, NULL);
    printf("Async I/O initialized\n");
}

// Shutdown async I/O
void async_shutdown(void) {
    reactor_running = 0;
    pthread_join(reactor_thread, NULL);
    close(epoll_fd);
    printf("Async I/O shutdown\n");
}

// Echo client handler (runs in its own thread for demo)
void* handle_client(void* arg) {
    int client_fd = *(int*)arg;
    free(arg);
    
    char buffer[BUFFER_SIZE];
    char response[] = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello World!\r\n";
    
    printf("Client %d connected\n", client_fd);
    
    while (1) {
        future_t* read_f = async_read(client_fd, buffer, BUFFER_SIZE - 1);
        int n = future_wait(read_f);
        free(read_f);
        
        if (n <= 0) break;
        
        buffer[n] = '\0';
        printf("Client %d sent: %s", client_fd, buffer);
        
        future_t* write_f = async_write(client_fd, response, strlen(response));
        future_wait(write_f);
        free(write_f);
    }
    
    close(client_fd);
    printf("Client %d disconnected\n", client_fd);
    return NULL;
}

int main() {
    printf("╔════════════════════════════════════════════╗\n");
    printf("║     Async I/O Echo Server                  ║\n");
    printf("╚════════════════════════════════════════════╝\n\n");
    
    // Initialize async I/O
    async_init();
    
    // Create server socket
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
    
    set_nonblocking(server_fd);
    
    printf("Echo server listening on port %d\n", PORT);
    printf("\nTest commands:\n");
    printf("  curl http://localhost:%d\n", PORT);
    printf("  telnet localhost %d\n", PORT);
    printf("  Press Ctrl+C to stop\n\n");
    
    // Register server socket with epoll
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev);
    
    // Main accept loop
    while (1) {
        struct epoll_event events[10];
        int nfds = epoll_wait(epoll_fd, events, 10, -1);
        
        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == server_fd) {
                // New connection
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
                
                if (client_fd > 0) {
                    printf("New connection from %s:%d\n", 
                           inet_ntoa(client_addr.sin_addr), 
                           ntohs(client_addr.sin_port));
                    
                    int* fd_ptr = malloc(sizeof(int));
                    *fd_ptr = client_fd;
                    
                    pthread_t tid;
                    pthread_create(&tid, NULL, handle_client, fd_ptr);
                    pthread_detach(tid);
                }
            }
        }
    }
    
    close(server_fd);
    async_shutdown();
    
    return 0;
}
