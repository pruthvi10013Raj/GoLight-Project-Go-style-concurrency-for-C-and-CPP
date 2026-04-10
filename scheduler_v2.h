#ifndef SCHEDULER_V2_H
#define SCHEDULER_V2_H

#include "goroutine.h"
#include <pthread.h>
#include <stdatomic.h>

#define DEFAULT_WORKER_COUNT 0  // 0 = auto-detect CPU cores
#define MAX_WORKERS 64
#define LOCAL_QUEUE_SIZE 256

// Worker thread structure
typedef struct worker {
    int id;
    pthread_t thread;
    
    // Local goroutine queue (will become lock-free deque later)
    goroutine_t* local_queue[LOCAL_QUEUE_SIZE];
    int queue_head;
    int queue_tail;
    int queue_size;
    
    // Statistics
    atomic_int goroutines_executed;
    atomic_int steals_performed;
    atomic_int steals_received;
    
    // Synchronization
    pthread_mutex_t lock;
    pthread_cond_t cond;
    atomic_int is_idle;
    
    // Parent scheduler reference
    struct scheduler_v2* scheduler;
} worker_t;

// Multi-threaded scheduler
typedef struct scheduler_v2 {
    worker_t* workers;
    int worker_count;
    
    // Global queue (overflow)
    goroutine_t** global_queue;
    int global_queue_size;
    int global_queue_capacity;
    pthread_mutex_t global_queue_lock;
    
    // Control
    atomic_int running;
    atomic_int active_goroutines;
    
    // Statistics
    atomic_int total_steals;
} scheduler_v2_t;

// API functions
int scheduler_v2_init(scheduler_v2_t* s, int worker_count);
void scheduler_v2_add(scheduler_v2_t* s, goroutine_t* g);
void scheduler_v2_start(scheduler_v2_t* s);
void scheduler_v2_wait(scheduler_v2_t* s);
void scheduler_v2_destroy(scheduler_v2_t* s);

#endif

