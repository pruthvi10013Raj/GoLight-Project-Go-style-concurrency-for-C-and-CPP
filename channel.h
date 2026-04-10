#ifndef CHANNEL_H
#define CHANNEL_H

#include <pthread.h>

typedef struct channel {
    void **buffer;
    int capacity;
    int size;
    int head;
    int tail;
    
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} channel_t;

// Channel functions
channel_t* channel_create(int capacity);
void channel_destroy(channel_t *ch);
void channel_send(channel_t *ch, void *data);
void* channel_receive(channel_t *ch);

#endif
