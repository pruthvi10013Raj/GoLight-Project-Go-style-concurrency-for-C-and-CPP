#include "deque.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Initialize deque
int deque_init(deque_t* d, size_t initial_capacity) {
    // Capacity must be power of 2
    size_t cap = 1;
    while (cap < initial_capacity) cap <<= 1;
    
    d->buffer = calloc(cap, sizeof(goroutine_t*));
    if (!d->buffer) return -1;
    
    d->capacity = cap;
    d->mask = cap - 1;
    d->bottom = 0;
    atomic_init(&d->top, 0);
    
    return 0;
}

// Destroy deque
void deque_destroy(deque_t* d) {
    free(d->buffer);
    d->buffer = NULL;
}

// Push to bottom (owner only)
void deque_push_bottom(deque_t* d, goroutine_t* g) {
    size_t b = d->bottom;
    size_t t = atomic_load(&d->top);
    
    // Resize if necessary
    if (b - t >= d->capacity - 1) {
        deque_resize(d);
        // After resize, recalculate
        b = d->bottom;
        t = atomic_load(&d->top);
    }
    
    d->buffer[b & d->mask] = g;
    
    // Release barrier: ensure buffer write is visible before bottom update
    atomic_thread_fence(memory_order_release);
    
    d->bottom = b + 1;
}

// Pop from bottom (owner only)
goroutine_t* deque_pop_bottom(deque_t* d) {
    size_t b = d->bottom;
    if (b == 0) return NULL;
    
    b--;
    d->bottom = b;
    
    size_t t = atomic_load(&d->top);
    
    // Acquire barrier: ensure we see buffer writes
    atomic_thread_fence(memory_order_acquire);
    
    goroutine_t* g = d->buffer[b & d->mask];
    
    if (b > t) {
        // Queue not empty
        return g;
    }
    
    if (b == t) {
        // Last item - try to claim it
        atomic_compare_exchange_strong(&d->top, &t, t + 1);
        d->bottom = 0;
        atomic_store(&d->top, 0);
        return g;
    }
    
    // Race lost or queue empty
    d->bottom = 0;
    return NULL;
}

// Steal from top (called by other workers)
goroutine_t* deque_steal(deque_t* d) {
    size_t t = atomic_load(&d->top);
    
    // Acquire barrier
    atomic_thread_fence(memory_order_acquire);
    
    size_t b = d->bottom;
    
    if (t >= b) {
        // Queue empty
        return NULL;
    }
    
    goroutine_t* g = d->buffer[t & d->mask];
    
    // Try to increment top (claim the item)
    if (atomic_compare_exchange_strong(&d->top, &t, t + 1)) {
        return g;
    }
    
    // Contention - steal failed
    return NULL;
}

// Resize deque (double capacity)
void deque_resize(deque_t* d) {
    size_t old_capacity = d->capacity;
    size_t new_capacity = old_capacity * 2;
    
    goroutine_t** new_buffer = calloc(new_capacity, sizeof(goroutine_t*));
    if (!new_buffer) return;
    
    size_t t = atomic_load(&d->top);
    size_t b = d->bottom;
    size_t size = b - t;
    
    // Copy elements
    for (size_t i = 0; i < size; i++) {
        new_buffer[i] = d->buffer[(t + i) & d->mask];
    }
    
    free(d->buffer);
    d->buffer = new_buffer;
    d->capacity = new_capacity;
    d->mask = new_capacity - 1;
    d->bottom = size;
    atomic_store(&d->top, 0);
}

