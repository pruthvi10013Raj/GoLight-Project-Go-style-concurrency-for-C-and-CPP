#ifndef DEQUE_H
#define DEQUE_H

#include "goroutine.h"
#include <stdatomic.h>
#include <stddef.h>

// Chase-Lev work-stealing deque
typedef struct deque {
    goroutine_t** buffer;      // Circular buffer
    size_t capacity;            // Must be power of 2
    size_t mask;                // capacity - 1
    
    // Bottom index (owned by worker)
    // Only accessed by owner, not atomic
    size_t bottom;
    
    // Top index (accessed by thieves)
    // Atomic for concurrent access
    atomic_size_t top;
} deque_t;

// Function declarations
int deque_init(deque_t* d, size_t initial_capacity);
void deque_destroy(deque_t* d);
void deque_push_bottom(deque_t* d, goroutine_t* g);
goroutine_t* deque_pop_bottom(deque_t* d);
goroutine_t* deque_steal(deque_t* d);
void deque_resize(deque_t* d);

#endif

