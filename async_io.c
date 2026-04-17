#include "async_io.h"
#include "worker_stealing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>

static io_reactor_t reactor;
static pthread_t reactor_thread;
static scheduler_stealing_t* global_scheduler = NULL;

// Set non-blocking flag on file descriptor
static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int async_io_init(void) {
    memset(&reactor, 0, sizeof(reactor));
    
    reactor.epoll_fd = epoll_create1(0);
    if (reactor.epoll_fd == -1) {
        perror("epoll_create1");
        return -1;
    }
    
    pthread_mutex_init(&reactor.lock, NULL);
    reactor.running = 1;
    
    // Create reactor thread
    if (pthread_create(&reactor_thread, NULL, io_reactor_thread, NULL) != 0) {
        perror("pthread_create");
        close(reactor.epoll_fd);
        return -1;
    }
    
    printf("Async I/O reactor initialized (epoll_fd=%d)\n", reactor.epoll_fd);
    return 0;
}

void async_io_shutdown(void) {
    reactor.running = 0;
    pthread_join(reactor_thread, NULL);
    close(reactor.epoll_fd);
    pthread_mutex_destroy(&reactor.lock);
    printf("Async I/O reactor shutdown\n");
}

// Helper to register fd with epoll
static int register_io(int fd, io_event_t* event, uint32_t events) {
    struct epoll_event ev;
    ev.events = events;
    ev.data.ptr = event;
    
    if (epoll_ctl(reactor.epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        // If already registered, modify
        if (errno == EEXIST) {
            if (epoll_ctl(reactor.epoll_fd, EPOLL_CTL_MOD, fd, &ev) == -1) {
                perror("epoll_ctl MOD");
                return -1;
            }
        } else {
            perror("epoll_ctl ADD");
            return -1;
        }
    }
    
    return 0;
}

future_t* async_read(int fd, void* buf, size_t count) {
    future_t* f = calloc(1, sizeof(future_t));
    if (!f) return NULL;
    
    io_event_t* event = calloc(1, sizeof(io_event_t));
    if (!event) {
        free(f);
        return NULL;
    }
    
    event->fd = fd;
    event->op = IO_READ;
    event->future = f;
    event->buffer = buf;
    event->count = count;
    
    // Set non-blocking
    set_nonblocking(fd);
    
    // Try immediate read
    ssize_t n = read(fd, buf, count);
    if (n >= 0) {
        // Data available immediately
        f->completed = 1;
        f->result = n;
        free(event);
        return f;
    }
    
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
        // Real error
        f->completed = 1;
        f->result = -1;
        free(event);
        return f;
    }
    
    // Would block - register with epoll
    f->waiting_goroutine = get_current_goroutine();  // Need to implement this
    event->future = f;
    
    pthread_mutex_lock(&reactor.lock);
    
    // Add to pending reads
    event->next = reactor.pending_reads;
    reactor.pending_reads = event;
    
    // Register with epoll
    register_io(fd, event, EPOLLIN | EPOLLONESHOT);
    
    pthread_mutex_unlock(&reactor.lock);
    
    // Park the goroutine
    park_goroutine();
    
    return f;
}

future_t* async_write(int fd, const void* buf, size_t count) {
    future_t* f = calloc(1, sizeof(future_t));
    if (!f) return NULL;
    
    io_event_t* event = calloc(1, sizeof(io_event_t));
    if (!event) {
        free(f);
        return NULL;
    }
    
    event->fd = fd;
    event->op = IO_WRITE;
    event->future = f;
    event->buffer = (void*)buf;
    event->count = count;
    
    set_nonblocking(fd);
    
    // Try immediate write
    ssize_t n = write(fd, buf, count);
    if (n >= 0) {
        f->completed = 1;
        f->result = n;
        free(event);
        return f;
    }
    
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
        f->completed = 1;
        f->result = -1;
        free(event);
        return f;
    }
    
    // Would block - register
    f->waiting_goroutine = get_current_goroutine();
    event->future = f;
    
    pthread_mutex_lock(&reactor.lock);
    
    event->next = reactor.pending_writes;
    reactor.pending_writes = event;
    
    register_io(fd, event, EPOLLOUT | EPOLLONESHOT);
    
    pthread_mutex_unlock(&reactor.lock);
    
    park_goroutine();
    
    return f;
}

future_t* async_sleep(uint64_t milliseconds) {
    future_t* f = calloc(1, sizeof(future_t));
    if (!f) return NULL;
    
    f->waiting_goroutine = get_current_goroutine();
    
    // For simplicity, create a timer thread or use timerfd
    // This is a simplified version
    pthread_t timer_thread;
    struct timer_data {
        future_t* f;
        uint64_t ms;
    }* data = malloc(sizeof(*data));
    data->f = f;
    data->ms = milliseconds;
    
    pthread_create(&timer_thread, NULL, timer_thread_func, data);
    pthread_detach(timer_thread);
    
    park_goroutine();
    
    return f;
}

void* io_reactor_thread(void* arg) {
    printf("I/O Reactor thread started\n");
    
    while (reactor.running) {
        int nfds = epoll_wait(reactor.epoll_fd, reactor.events, 1024, 100);
        
        for (int i = 0; i < nfds; i++) {
            io_event_t* event = (io_event_t*)reactor.events[i].data.ptr;
            future_t* f = event->future;
            
            // Perform the actual I/O
            if (event->op == IO_READ) {
                ssize_t n = read(event->fd, event->buffer, event->count);
                f->result = n;
            } else if (event->op == IO_WRITE) {
                ssize_t n = write(event->fd, event->buffer, event->count);
                f->result = n;
            }
            
            f->completed = 1;
            
            // Wake up the waiting goroutine
            if (f->waiting_goroutine) {
                scheduler_ready(f->waiting_goroutine);
            }
            
            // Remove from epoll
            epoll_ctl(reactor.epoll_fd, EPOLL_CTL_DEL, event->fd, NULL);
            free(event);
        }
    }
    
    return NULL;
}

int future_wait(future_t* f) {
    while (!f->completed) {
        usleep(1000);
    }
    return f->result;
}

int future_try_wait(future_t* f) {
    return f->completed;
}

void future_free(future_t* f) {
    free(f);
}
