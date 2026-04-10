#ifndef WORKER_STEALING_H
#define WORKER_STEALING_H

#include "goroutine.h"
#include "deque.h"
#include <pthread.h>
#include <stdatomic.h>

typedef struct scheduler_stealing scheduler_stealing_t;

typedef struct worker_stealing {
    int id;
    pthread_t thread;
    
    // Work-stealing deque
    deque_t deque;
    
    // Statistics
    atomic_int goroutines_executed;
    atomic_int steals_done;
    atomic_int steals_received;
    
    // Synchronization
    pthread_mutex_t lock;
    pthread_cond_t cond;
    atomic_int is_idle;
    
    // Parent scheduler
    scheduler_stealing_t* scheduler;
} worker_stealing_t;

typedef struct scheduler_stealing {
    worker_stealing_t* workers;
    int worker_count;
    
    // Global queue (fallback)
    deque_t global_deque;
    
    // Control
    atomic_int running;
    atomic_int active_goroutines;
    
    // Statistics
    atomic_int total_steals;
} scheduler_stealing_t;

// API
int scheduler_stealing_init(scheduler_stealing_t* s, int worker_count);
void scheduler_stealing_add(scheduler_stealing_t* s, goroutine_t* g);
void scheduler_stealing_start(scheduler_stealing_t* s);
void scheduler_stealing_wait(scheduler_stealing_t* s);
void scheduler_stealing_destroy(scheduler_stealing_t* s);

#endif
