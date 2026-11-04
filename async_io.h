#ifndef ASYNC_IO_H
#define ASYNC_IO_H

#include <sys/epoll.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>
#include <stdatomic.h>

// I/O operation types
typedef enum {
    IO_READ,
    IO_WRITE,
    IO_ACCEPT,
    IO_TIMEOUT
} io_op_t;

// Future/Promise for async operations
typedef struct future {
    atomic_int completed;
    int result;
    void* data;
    size_t size;
    struct goroutine* waiting_goroutine;
    struct future* next;
} future_t;

// I/O event structure
typedef struct io_event {
    int fd;
    io_op_t op;
    future_t* future;
    void* buffer;
    size_t count;
    struct io_event* next;
} io_event_t;

// I/O Reactor
typedef struct io_reactor {
    int epoll_fd;
    struct epoll_event events[1024];
    
    // Pending operations
    io_event_t* pending_reads;
    io_event_t* pending_writes;
    
    pthread_mutex_t lock;
    int running;
} io_reactor_t;

// Function declarations
int async_io_init(void);
void async_io_shutdown(void);

// Async I/O operations
future_t* async_read(int fd, void* buf, size_t count);
future_t* async_write(int fd, const void* buf, size_t count);
future_t* async_accept(int fd);
future_t* async_sleep(uint64_t milliseconds);

// Future operations
int future_wait(future_t* f);
int future_try_wait(future_t* f);
void future_free(future_t* f);

// Reactor thread
void* io_reactor_thread(void* arg);

#endif
