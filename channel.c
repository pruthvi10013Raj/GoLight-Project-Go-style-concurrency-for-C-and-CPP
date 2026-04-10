#include "channel.h"
#include <stdio.h>
#include <stdlib.h>

channel_t* channel_create(int capacity) {
    channel_t *ch = malloc(sizeof(channel_t));
    if (!ch) return NULL;
    
    ch->capacity = capacity;
    ch->size = 0;
    ch->head = 0;
    ch->tail = 0;
    
    ch->buffer = malloc(capacity * sizeof(void*));
    if (!ch->buffer) {
        free(ch);
        return NULL;
    }
    
    pthread_mutex_init(&ch->lock, NULL);
    pthread_cond_init(&ch->not_empty, NULL);
    pthread_cond_init(&ch->not_full, NULL);
    
    return ch;
}

void channel_destroy(channel_t *ch) {
    pthread_mutex_destroy(&ch->lock);
    pthread_cond_destroy(&ch->not_empty);
    pthread_cond_destroy(&ch->not_full);
    free(ch->buffer);
    free(ch);
}

void channel_send(channel_t *ch, void *data) {
    pthread_mutex_lock(&ch->lock);
    
    // Wait if buffer is full
    while (ch->size == ch->capacity) {
        pthread_cond_wait(&ch->not_full, &ch->lock);
    }
    
    // Add data to buffer
    ch->buffer[ch->tail] = data;
    ch->tail = (ch->tail + 1) % ch->capacity;
    ch->size++;
    
    // Signal that buffer is not empty
    pthread_cond_signal(&ch->not_empty);
    
    pthread_mutex_unlock(&ch->lock);
}

void* channel_receive(channel_t *ch) {
    pthread_mutex_lock(&ch->lock);
    
    // Wait if buffer is empty
    while (ch->size == 0) {
        pthread_cond_wait(&ch->not_empty, &ch->lock);
    }
    
    // Get data from buffer
    void *data = ch->buffer[ch->head];
    ch->head = (ch->head + 1) % ch->capacity;
    ch->size--;
    
    // Signal that buffer is not full
    pthread_cond_signal(&ch->not_full);
    
    pthread_mutex_unlock(&ch->lock);
    
    return data;
}
