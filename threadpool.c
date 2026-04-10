#include "threadpool.h"
#include <stdio.h>
#include <stdlib.h>

static void* threadpool_worker(void* arg) {
    threadpool_t* pool = (threadpool_t*)arg;
    
    while (atomic_load(&pool->running)) {
        threadpool_task_t* task = NULL;
        
        pthread_mutex_lock(&pool->task_queue_lock);
        
        while (pool->task_queue_size == 0 && atomic_load(&pool->running)) {
            pthread_cond_wait(&pool->task_queue_cond, &pool->task_queue_lock);
        }
        
        if (pool->task_queue_size > 0) {
            task = pool->task_queue_head;
            pool->task_queue_head = task->next;
            pool->task_queue_size--;
            
            if (pool->task_queue_size == 0) {
                pool->task_queue_tail = NULL;
            }
        }
        
        pthread_mutex_unlock(&pool->task_queue_lock);
        
        if (task) {
            // Execute task
            task->function(task->arg);
            atomic_fetch_add(&pool->tasks_completed, 1);
            free(task);
        }
    }
    
    return NULL;
}

int threadpool_init(threadpool_t* pool, int thread_count) {
    if (thread_count <= 0) {
        thread_count = sysconf(_SC_NPROCESSORS_ONLN) * 2;
    }
    
    pool->thread_count = thread_count;
    pool->threads = malloc(sizeof(pthread_t) * thread_count);
    
    pool->task_queue_head = NULL;
    pool->task_queue_tail = NULL;
    pool->task_queue_size = 0;
    pthread_mutex_init(&pool->task_queue_lock, NULL);
    pthread_cond_init(&pool->task_queue_cond, NULL);
    
    atomic_init(&pool->running, 1);
    atomic_init(&pool->active_tasks, 0);
    atomic_init(&pool->tasks_completed, 0);
    
    for (int i = 0; i < thread_count; i++) {
        pthread_create(&pool->threads[i], NULL, threadpool_worker, pool);
    }
    
    return 0;
}

void threadpool_submit(threadpool_t* pool, void (*function)(void*), void* arg) {
    threadpool_task_t* task = malloc(sizeof(threadpool_task_t));
    task->function = function;
    task->arg = arg;
    task->next = NULL;
    
    pthread_mutex_lock(&pool->task_queue_lock);
    
    if (pool->task_queue_tail) {
        pool->task_queue_tail->next = task;
    } else {
        pool->task_queue_head = task;
    }
    pool->task_queue_tail = task;
    pool->task_queue_size++;
    
    pthread_cond_signal(&pool->task_queue_cond);
    pthread_mutex_unlock(&pool->task_queue_lock);
    
    atomic_fetch_add(&pool->active_tasks, 1);
}

void threadpool_wait(threadpool_t* pool) {
    while (atomic_load(&pool->active_tasks) > 0 ||
           pool->task_queue_size > 0) {
        usleep(10000);
    }
}

void threadpool_destroy(threadpool_t* pool) {
    atomic_store(&pool->running, 0);
    pthread_cond_broadcast(&pool->task_queue_cond);
    
    for (int i = 0; i < pool->thread_count; i++) {
        pthread_join(pool->threads[i], NULL);
    }
    
    free(pool->threads);
    pthread_mutex_destroy(&pool->task_queue_lock);
    pthread_cond_destroy(&pool->task_queue_cond);
}

