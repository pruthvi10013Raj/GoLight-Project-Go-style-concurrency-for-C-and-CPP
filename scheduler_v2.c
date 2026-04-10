#include "scheduler_v2.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>

// Forward declarations
static void* worker_loop(void* arg);
static void execute_goroutine(worker_t* w, goroutine_t* g);

// Initialize scheduler
int scheduler_v2_init(scheduler_v2_t* s, int worker_count) {
    // Auto-detect CPU count
    if (worker_count == DEFAULT_WORKER_COUNT) {
        worker_count = sysconf(_SC_NPROCESSORS_ONLN);
        if (worker_count < 1) worker_count = 2;
        // Use 2x CPU cores for hyper-threading
        worker_count = worker_count * 2;
        printf("Auto-detected %d CPU cores, creating %d workers\n", 
               worker_count / 2, worker_count);
    }
    
    if (worker_count > MAX_WORKERS) worker_count = MAX_WORKERS;
    
    s->worker_count = worker_count;
    s->workers = calloc(worker_count, sizeof(worker_t));
    if (!s->workers) return -1;
    
    // Initialize global queue
    s->global_queue_capacity = 10000;
    s->global_queue = malloc(sizeof(goroutine_t*) * s->global_queue_capacity);
    s->global_queue_size = 0;
    pthread_mutex_init(&s->global_queue_lock, NULL);
    
    // Initialize workers
    for (int i = 0; i < worker_count; i++) {
        s->workers[i].id = i;
        s->workers[i].queue_head = 0;
        s->workers[i].queue_tail = 0;
        s->workers[i].queue_size = 0;
        s->workers[i].scheduler = s;
        s->workers[i].is_idle = 0;
        
        pthread_mutex_init(&s->workers[i].lock, NULL);
        pthread_cond_init(&s->workers[i].cond, NULL);
        
        atomic_init(&s->workers[i].goroutines_executed, 0);
        atomic_init(&s->workers[i].steals_performed, 0);
        atomic_init(&s->workers[i].steals_received, 0);
    }
    
    atomic_init(&s->running, 0);
    atomic_init(&s->active_goroutines, 0);
    atomic_init(&s->total_steals, 0);
    
    return 0;
}

// Add goroutine to scheduler
void scheduler_v2_add(scheduler_v2_t* s, goroutine_t* g) {
    if (!g) return;
    
    atomic_fetch_add(&s->active_goroutines, 1);
    
    // Try to add to least loaded worker's local queue
    int min_load = 999999;
    int target_worker = 0;
    
    for (int i = 0; i < s->worker_count; i++) {
        int load = s->workers[i].queue_size;
        if (load < min_load) {
            min_load = load;
            target_worker = i;
        }
    }
    
    worker_t* w = &s->workers[target_worker];
    
    pthread_mutex_lock(&w->lock);
    
    if (w->queue_size < LOCAL_QUEUE_SIZE) {
        // Add to local queue
        w->local_queue[w->queue_tail] = g;
        w->queue_tail = (w->queue_tail + 1) % LOCAL_QUEUE_SIZE;
        w->queue_size++;
        
        // Wake up worker if idle
        if (atomic_load(&w->is_idle)) {
            pthread_cond_signal(&w->cond);
        }
    } else {
        // Local queue full, add to global queue
        pthread_mutex_lock(&s->global_queue_lock);
        if (s->global_queue_size < s->global_queue_capacity) {
            s->global_queue[s->global_queue_size++] = g;
        } else {
            fprintf(stderr, "Global queue full! Increase capacity.\n");
        }
        pthread_mutex_unlock(&s->global_queue_lock);
    }
    
    pthread_mutex_unlock(&w->lock);
}

// Start all workers
void scheduler_v2_start(scheduler_v2_t* s) {
    atomic_store(&s->running, 1);
    
    // Create worker threads
    for (int i = 0; i < s->worker_count; i++) {
        pthread_create(&s->workers[i].thread, NULL, worker_loop, &s->workers[i]);
        printf("Started worker %d\n", i);
    }
}

// Wait for all goroutines to complete
void scheduler_v2_wait(scheduler_v2_t* s) {
    while (atomic_load(&s->active_goroutines) > 0) {
        usleep(10000);  // 10ms
    }
    
    atomic_store(&s->running, 0);
    
    // Wake up all workers to exit
    for (int i = 0; i < s->worker_count; i++) {
        pthread_cond_broadcast(&s->workers[i].cond);
        pthread_join(s->workers[i].thread, NULL);
    }
}

// Worker thread main loop (basic version - will add stealing later)
static void* worker_loop(void* arg) {
    worker_t* w = (worker_t*)arg;
    scheduler_v2_t* s = w->scheduler;
    
    printf("Worker %d started on CPU %d\n", w->id, sched_getcpu());
    
    while (atomic_load(&s->running)) {
        goroutine_t* g = NULL;
        
        // Try to get from local queue
        pthread_mutex_lock(&w->lock);
        
        if (w->queue_size > 0) {
            g = w->local_queue[w->queue_head];
            w->queue_head = (w->queue_head + 1) % LOCAL_QUEUE_SIZE;
            w->queue_size--;
        }
        
        pthread_mutex_unlock(&w->lock);
        
        // If local queue empty, try global queue
        if (!g) {
            pthread_mutex_lock(&s->global_queue_lock);
            if (s->global_queue_size > 0) {
                g = s->global_queue[--s->global_queue_size];
            }
            pthread_mutex_unlock(&s->global_queue_lock);
        }
        
        // Execute goroutine
        if (g) {
            execute_goroutine(w, g);
        } else {
            // No work - mark idle and wait
            atomic_store(&w->is_idle, 1);
            
            pthread_mutex_lock(&w->lock);
            pthread_cond_timedwait(&w->cond, &w->lock, 
                                   &(struct timespec){.tv_sec = 0, .tv_nsec = 10000000});
            pthread_mutex_unlock(&w->lock);
            
            atomic_store(&w->is_idle, 0);
        }
    }
    
    return NULL;
}

// Execute a goroutine
static void execute_goroutine(worker_t* w, goroutine_t* g) {
    printf("Worker %d executing goroutine %d\n", w->id, g->id);
    
    // Run the goroutine
    g->state = GOROUTINE_RUNNING;
    
    // Switch context to goroutine
    ucontext_t* current = get_current_context();
    swapcontext(current, &g->context);
    
    // When we return, goroutine finished
    g->state = GOROUTINE_FINISHED;
    atomic_fetch_add(&w->goroutines_executed, 1);
    atomic_fetch_sub(&w->scheduler->active_goroutines, 1);
    
    printf("Worker %d finished goroutine %d\n", w->id, g->id);
    
    // Free goroutine
    free_goroutine(g);
}

// Helper to get current context (simplified)
static ucontext_t* get_current_context(void) {
    static __thread ucontext_t ctx;
    return &ctx;
}
