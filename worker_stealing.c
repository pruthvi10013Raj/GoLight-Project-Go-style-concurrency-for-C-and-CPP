#include "worker_stealing.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

static void* worker_stealing_loop(void* arg);
static goroutine_t* try_steal(worker_stealing_t* thief);
static void execute_goroutine_stealing(worker_stealing_t* w, goroutine_t* g);

int scheduler_stealing_init(scheduler_stealing_t* s, int worker_count) {
    if (worker_count <= 0) {
        worker_count = sysconf(_SC_NPROCESSORS_ONLN) * 2;
        if (worker_count < 1) worker_count = 2;
    }
    
    printf("Initializing scheduler with %d workers\n", worker_count);
    
    s->worker_count = worker_count;
    s->workers = calloc(worker_count, sizeof(worker_stealing_t));
    if (!s->workers) return -1;
    
    // Initialize global deque
    deque_init(&s->global_deque, 1024);
    
    // Initialize workers
    for (int i = 0; i < worker_count; i++) {
        s->workers[i].id = i;
        s->workers[i].scheduler = s;
        deque_init(&s->workers[i].deque, 64);
        
        pthread_mutex_init(&s->workers[i].lock, NULL);
        pthread_cond_init(&s->workers[i].cond, NULL);
        
        atomic_init(&s->workers[i].goroutines_executed, 0);
        atomic_init(&s->workers[i].steals_done, 0);
        atomic_init(&s->workers[i].steals_received, 0);
        atomic_init(&s->workers[i].is_idle, 0);
    }
    
    atomic_init(&s->running, 0);
    atomic_init(&s->active_goroutines, 0);
    atomic_init(&s->total_steals, 0);
    
    return 0;
}

void scheduler_stealing_add(scheduler_stealing_t* s, goroutine_t* g) {
    if (!g) return;
    
    atomic_fetch_add(&s->active_goroutines, 1);
    
    // Round-robin distribution for better load balance
    static int next_worker = 0;
    int target = next_worker++ % s->worker_count;
    worker_stealing_t* w = &s->workers[target];
    
    deque_push_bottom(&w->deque, g);
    printf("Added goroutine %d to worker %d queue\n", g->id, target);
    
    // Wake up worker if idle
    if (atomic_load(&w->is_idle)) {
        pthread_mutex_lock(&w->lock);
        pthread_cond_signal(&w->cond);
        pthread_mutex_unlock(&w->lock);
    }
}

void scheduler_stealing_start(scheduler_stealing_t* s) {
    atomic_store(&s->running, 1);
    
    for (int i = 0; i < s->worker_count; i++) {
        pthread_create(&s->workers[i].thread, NULL, 
                      worker_stealing_loop, &s->workers[i]);
        printf("Worker %d started (with work-stealing)\n", i);
    }
}

void scheduler_stealing_wait(scheduler_stealing_t* s) {
    printf("\nWaiting for %d goroutines to complete...\n", 
           atomic_load(&s->active_goroutines));
    
    // Wait for all goroutines to complete
    int last_count = -1;
    while (atomic_load(&s->active_goroutines) > 0) {
        int current = atomic_load(&s->active_goroutines);
        if (current != last_count) {
            printf("  %d goroutines remaining...\n", current);
            last_count = current;
        }
        usleep(100000);  // 100ms
    }
    
    printf("All goroutines completed!\n");
    
    // Stop workers
    atomic_store(&s->running, 0);
    
    // Wake up all workers and wait for them to exit
    for (int i = 0; i < s->worker_count; i++) {
        pthread_cond_broadcast(&s->workers[i].cond);
        pthread_join(s->workers[i].thread, NULL);
    }
    
    printf("\n=== Work-Stealing Statistics ===\n");
    int total_executed = 0;
    for (int i = 0; i < s->worker_count; i++) {
        int executed = atomic_load(&s->workers[i].goroutines_executed);
        int steals_done = atomic_load(&s->workers[i].steals_done);
        int steals_recv = atomic_load(&s->workers[i].steals_received);
        total_executed += executed;
        
        printf("Worker %d: %d goroutines, %d steals done, %d steals received\n",
               i, executed, steals_done, steals_recv);
    }
    printf("Total goroutines executed: %d\n", total_executed);
    printf("Total steals: %d\n", atomic_load(&s->total_steals));
}

void scheduler_stealing_destroy(scheduler_stealing_t* s) {
    if (!s) return;
    
    // Free all workers' deques
    for (int i = 0; i < s->worker_count; i++) {
        deque_destroy(&s->workers[i].deque);
        pthread_mutex_destroy(&s->workers[i].lock);
        pthread_cond_destroy(&s->workers[i].cond);
    }
    
    // Free global deque
    deque_destroy(&s->global_deque);
    
    // Free workers array
    free(s->workers);
    
    printf("Scheduler destroyed\n");
}

static void* worker_stealing_loop(void* arg) {
    worker_stealing_t* w = (worker_stealing_t*)arg;
    scheduler_stealing_t* s = w->scheduler;
    
    printf("Worker %d running on CPU %d\n", w->id, sched_getcpu());
    
    while (atomic_load(&s->running)) {
        goroutine_t* g = NULL;
        
        // 1. Try to pop from local deque
        g = deque_pop_bottom(&w->deque);
        
        // 2. If local empty, try to steal
        if (!g && atomic_load(&s->active_goroutines) > 0) {
            g = try_steal(w);
        }
        
        // 3. If still empty, try global queue
        if (!g && atomic_load(&s->active_goroutines) > 0) {
            g = deque_steal(&s->global_deque);
        }
        
        // 4. Execute if we found work
        if (g) {
            execute_goroutine_stealing(w, g);
        } else if (atomic_load(&s->active_goroutines) == 0) {
            // No work and no active goroutines - exit
            break;
        } else {
            // No work but goroutines exist - park briefly
            atomic_store(&w->is_idle, 1);
            
            pthread_mutex_lock(&w->lock);
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += 50000000;  // 50ms timeout
            pthread_cond_timedwait(&w->cond, &w->lock, &ts);
            pthread_mutex_unlock(&w->lock);
            
            atomic_store(&w->is_idle, 0);
        }
    }
    
    printf("Worker %d exiting\n", w->id);
    return NULL;
}

// Try to steal from another worker
static goroutine_t* try_steal(worker_stealing_t* thief) {
    scheduler_stealing_t* s = thief->scheduler;
    const int MAX_STEAL_ATTEMPTS = 3;
    
    for (int attempt = 0; attempt < MAX_STEAL_ATTEMPTS; attempt++) {
        // Pick random victim (avoid stealing from self)
        int victim_id;
        do {
            victim_id = rand() % s->worker_count;
        } while (victim_id == thief->id);
        
        worker_stealing_t* victim = &s->workers[victim_id];
        
        // Try to steal from victim's deque
        goroutine_t* g = deque_steal(&victim->deque);
        
        if (g) {
            // Steal successful!
            atomic_fetch_add(&thief->steals_done, 1);
            atomic_fetch_add(&victim->steals_received, 1);
            atomic_fetch_add(&s->total_steals, 1);
            
            printf("Worker %d stole goroutine %d from worker %d\n", 
                   thief->id, g->id, victim_id);
            return g;
        }
    }
    
    return NULL;
}

// FIXED: The critical function - proper context switching
static void execute_goroutine_stealing(worker_stealing_t* w, goroutine_t* g) {
    printf("Worker %d: Executing goroutine %d\n", w->id, g->id);
    
    // Save worker's current context
    ucontext_t worker_context;
    getcontext(&worker_context);
    
    // Set the goroutine to return to this worker context when done
    // This is the key fix!
    g->context.uc_link = &worker_context;
    
    // Mark goroutine as running
    g->state = GOROUTINE_RUNNING;
    
    // Switch to goroutine
    // When goroutine finishes, it will automatically return to worker_context
    swapcontext(&worker_context, &g->context);
    
    // We return here when the goroutine finishes (via uc_link)
    printf("Worker %d: Goroutine %d completed\n", w->id, g->id);
    
    // Update statistics
    g->state = GOROUTINE_FINISHED;
    atomic_fetch_add(&w->goroutines_executed, 1);
    atomic_fetch_sub(&w->scheduler->active_goroutines, 1);
    
    // Free the goroutine
    free_goroutine(g);
}
