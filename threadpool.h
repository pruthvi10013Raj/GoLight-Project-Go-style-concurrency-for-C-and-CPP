#ifndef THREADPOOL_H
#define THREADPOOL_H

#include "goroutine.h"
#include <pthread.h>
#include <stdatomic.h>

typedef struct threadpool_task {
    void (*function)(void*);
    void* arg;
    struct threadpool_task* next;
} threadpool_task_t;

typedef struct threadpool {
    pthread_t* threads;
    int thread_count;
    
    // Task queue
    threadpool_task_t* task_queue_head;
    threadpool_task_t* task_queue_tail;
    int task_queue_size;
    pthread_mutex_t task_queue_lock;
    pthread_cond_t task_queue_cond;
    
    // Control
    atomic_int running;
    atomic_int active_tasks;
    
    // Statistics
    atomic_int tasks_completed;
} threadpool_t;

// API
int threadpool_init(threadpool_t* pool, int thread_count);
void threadpool_submit(threadpool_t* pool, void (*function)(void*), void* arg);
void threadpool_wait(threadpool_t* pool);
void threadpool_destroy(threadpool_t* pool);

#endif

