#ifndef CHANNEL_H
#define CHANNEL_H

#include <pthread.h>
#include <stdatomic.h>

struct goroutine;

typedef struct channel {
    void **buffer;
    int capacity;
    int size;
    int head;
    int tail;
    pthread_mutex_t lock;
    struct goroutine *send_waiters;
    struct goroutine *recv_waiters;
    atomic_int closed;
    atomic_int references;
} channel_t;

// Channel functions
channel_t* channel_create(int capacity);
void channel_destroy(channel_t *ch);
void channel_send(channel_t *ch, void *data);
void* channel_receive(channel_t *ch);

#endif
