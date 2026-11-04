#include "channel.h"
#include "goroutine.h"
#include "worker_stealing.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static void enqueue_waiter(goroutine_t** queue, goroutine_t* g, channel_t* ch) {
    g->wait_channel = ch;
    g->wait_next = NULL;
    while (*queue) queue = &(*queue)->wait_next;
    *queue = g;
}

static goroutine_t* dequeue_waiter(goroutine_t** queue) {
    goroutine_t* g = *queue;
    if (g) {
        *queue = (goroutine_t*)g->wait_next;
        g->wait_next = NULL;
        g->wait_channel = NULL;
    }
    return g;
}

static void channel_release(channel_t* ch) {
    if (atomic_fetch_sub(&ch->references, 1) == 1) {
        pthread_mutex_destroy(&ch->lock);
        free(ch->buffer);
        free(ch);
    }
}

channel_t* channel_create(int capacity) {
    if (capacity < 0) {
        errno = EINVAL;
        return NULL;
    }

    channel_t *ch = malloc(sizeof(channel_t));
    if (!ch) return NULL;
    
    ch->capacity = capacity;
    ch->size = 0;
    ch->head = 0;
    ch->tail = 0;
    
    ch->buffer = capacity ? malloc((size_t)capacity * sizeof(void*)) : NULL;
    if (capacity && !ch->buffer) {
        free(ch);
        return NULL;
    }

    ch->send_waiters = NULL;
    ch->recv_waiters = NULL;
    atomic_init(&ch->closed, 0);
    atomic_init(&ch->references, 1);
    if (pthread_mutex_init(&ch->lock, NULL) != 0) {
        free(ch->buffer);
        free(ch);
        errno = ENOMEM;
        return NULL;
    }
    return ch;
}

void channel_destroy(channel_t *ch) {
    if (!ch) return;
    pthread_mutex_lock(&ch->lock);
    if (atomic_exchange(&ch->closed, 1)) {
        pthread_mutex_unlock(&ch->lock);
        return;
    }
    goroutine_t* send_waiters = ch->send_waiters;
    goroutine_t* recv_waiters = ch->recv_waiters;
    ch->send_waiters = NULL;
    ch->recv_waiters = NULL;
    for (goroutine_t* g = send_waiters; g; g = g->wait_next) g->wait_channel = NULL;
    for (goroutine_t* g = recv_waiters; g; g = g->wait_next) g->wait_channel = NULL;
    pthread_mutex_unlock(&ch->lock);

    while (send_waiters) {
        goroutine_t* next = send_waiters->wait_next;
        send_waiters->wait_next = NULL;
        scheduler_ready(send_waiters);
        send_waiters = next;
    }
    while (recv_waiters) {
        goroutine_t* next = recv_waiters->wait_next;
        recv_waiters->wait_next = NULL;
        scheduler_ready(recv_waiters);
        recv_waiters = next;
    }

    channel_release(ch);
}

void channel_send(channel_t *ch, void *data) {
    if (!ch) {
        errno = EINVAL;
        return;
    }
    atomic_fetch_add(&ch->references, 1);

    goroutine_t* current = get_current_goroutine();
    for (;;) {
        goroutine_t* wake_receiver = NULL;
        pthread_mutex_lock(&ch->lock);
        if (current && current->wait_value_ready) {
            current->wait_value_ready = 0;
            current->wait_value = NULL;
            pthread_mutex_unlock(&ch->lock);
            channel_release(ch);
            return;
        }
        if (atomic_load(&ch->closed)) {
            pthread_mutex_unlock(&ch->lock);
            errno = EPIPE;
            channel_release(ch);
            return;
        }

        wake_receiver = dequeue_waiter(&ch->recv_waiters);
        if (wake_receiver) {
            wake_receiver->wait_value = data;
            wake_receiver->wait_value_ready = 1;
            pthread_mutex_unlock(&ch->lock);
            scheduler_ready(wake_receiver);
            channel_release(ch);
            return;
        }

        if (ch->capacity > ch->size) {
            ch->buffer[ch->tail] = data;
            ch->tail = (ch->tail + 1) % ch->capacity;
            ch->size++;
            pthread_mutex_unlock(&ch->lock);
            channel_release(ch);
            return;
        }

        if (!current) {
            pthread_mutex_unlock(&ch->lock);
            errno = EWOULDBLOCK;
            channel_release(ch);
            return;
        }
        if (current->wait_channel != ch) {
            current->wait_value = data;
            enqueue_waiter(&ch->send_waiters, current, ch);
        }
        pthread_mutex_unlock(&ch->lock);
        park_goroutine();
    }
}

void* channel_receive(channel_t *ch) {
    if (!ch) {
        errno = EINVAL;
        return NULL;
    }
    atomic_fetch_add(&ch->references, 1);

    goroutine_t* current = get_current_goroutine();
    for (;;) {
        goroutine_t* wake_sender = NULL;
        pthread_mutex_lock(&ch->lock);
        if (current && current->wait_value_ready) {
            void* value = current->wait_value;
            current->wait_value = NULL;
            current->wait_value_ready = 0;
            pthread_mutex_unlock(&ch->lock);
            channel_release(ch);
            return value;
        }

        if (ch->size > 0) {
            void* value = ch->buffer[ch->head];
            ch->buffer[ch->head] = NULL;
            ch->head = (ch->head + 1) % ch->capacity;
            ch->size--;
            wake_sender = dequeue_waiter(&ch->send_waiters);
            pthread_mutex_unlock(&ch->lock);
            if (wake_sender) scheduler_ready(wake_sender);
            channel_release(ch);
            return value;
        }

        if (ch->capacity == 0) {
            wake_sender = dequeue_waiter(&ch->send_waiters);
            if (wake_sender) {
                void* value = wake_sender->wait_value;
                wake_sender->wait_value = NULL;
                wake_sender->wait_value_ready = 1;
                pthread_mutex_unlock(&ch->lock);
                scheduler_ready(wake_sender);
                channel_release(ch);
                return value;
            }
        }

        if (atomic_load(&ch->closed)) {
            pthread_mutex_unlock(&ch->lock);
            channel_release(ch);
            return NULL;
        }
        if (!current) {
            pthread_mutex_unlock(&ch->lock);
            errno = EWOULDBLOCK;
            channel_release(ch);
            return NULL;
        }
        if (current->wait_channel != ch) {
            current->wait_value = NULL;
            current->wait_value_ready = 0;
            enqueue_waiter(&ch->recv_waiters, current, ch);
        }
        pthread_mutex_unlock(&ch->lock);
        park_goroutine();
    }
}
