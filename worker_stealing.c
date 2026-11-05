#include "worker_stealing.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#include <sched.h>

static void* worker_stealing_loop(void* arg);
static goroutine_t* try_steal(worker_stealing_t* thief);
static void execute_goroutine_stealing(worker_stealing_t* w, goroutine_t* g);
static __thread goroutine_t* current_goroutine = NULL;
static __thread ucontext_t* current_worker_context = NULL;

int scheduler_stealing_init(scheduler_stealing_t* s, int worker_count) {
    if (worker_count <= 0) {
        long cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
        worker_count = cpu_count > 0 ? (int)(cpu_count * 2) : 2;
        if (worker_count > 64) worker_count = 64;
    }
    
    printf("Initializing scheduler with %d workers\n", worker_count);
    
    s->worker_count = worker_count;
    s->workers = calloc(worker_count, sizeof(worker_stealing_t));
    if (!s->workers) return -1;
    
    // The global deque is an injection queue, so external threads may push to it.
    if (deque_init(&s->global_deque, 1024) != 0) {
        free(s->workers);
        s->workers = NULL;
        return -1;
    }
    if (pthread_mutex_init(&s->global_deque_lock, NULL) != 0) {
        deque_destroy(&s->global_deque);
        free(s->workers);
        s->workers = NULL;
        return -1;
    }
    
    // Initialize workers
    for (int i = 0; i < worker_count; i++) {
        s->workers[i].id = i;
        s->workers[i].scheduler = s;
        if (deque_init(&s->workers[i].deque, 64) != 0) {
            for (int j = 0; j < i; j++) {
                deque_destroy(&s->workers[j].deque);
                pthread_mutex_destroy(&s->workers[j].deque_lock);
                pthread_mutex_destroy(&s->workers[j].lock);
                pthread_cond_destroy(&s->workers[j].cond);
            }
            deque_destroy(&s->global_deque);
            pthread_mutex_destroy(&s->global_deque_lock);
            free(s->workers);
            s->workers = NULL;
            return -1;
        }
        int init_error = pthread_mutex_init(&s->workers[i].deque_lock, NULL);
        if (init_error == 0) {
            init_error = pthread_mutex_init(&s->workers[i].lock, NULL);
            if (init_error != 0) pthread_mutex_destroy(&s->workers[i].deque_lock);
        }
        if (init_error == 0) {
            init_error = pthread_cond_init(&s->workers[i].cond, NULL);
            if (init_error != 0) {
                pthread_mutex_destroy(&s->workers[i].lock);
                pthread_mutex_destroy(&s->workers[i].deque_lock);
            }
        }
        if (init_error != 0) {
            deque_destroy(&s->workers[i].deque);
            for (int j = 0; j < i; j++) {
                deque_destroy(&s->workers[j].deque);
                pthread_mutex_destroy(&s->workers[j].deque_lock);
                pthread_mutex_destroy(&s->workers[j].lock);
                pthread_cond_destroy(&s->workers[j].cond);
            }
            deque_destroy(&s->global_deque);
            pthread_mutex_destroy(&s->global_deque_lock);
            free(s->workers);
            s->workers = NULL;
            return -1;
        }
        
        atomic_init(&s->workers[i].goroutines_executed, 0);
        atomic_init(&s->workers[i].steals_done, 0);
        atomic_init(&s->workers[i].steals_received, 0);
        atomic_init(&s->workers[i].is_idle, 0);
    }
    
    atomic_init(&s->running, 0);
    atomic_init(&s->active_goroutines, 0);
    atomic_init(&s->next_worker, 0);
    atomic_init(&s->total_steals, 0);

    return 0;
}

void scheduler_stealing_add(scheduler_stealing_t* s, goroutine_t* g) {
    if (!g) return;
    int goroutine_id = g->id;
    
    g->scheduler = s;
    atomic_fetch_add(&s->active_goroutines, 1);
    
    atomic_store(&g->state, GOROUTINE_READY);
    if (!atomic_load(&s->running)) {
        int target = g->worker_id >= 0
                         ? g->worker_id
                         : atomic_fetch_add(&s->next_worker, 1) % s->worker_count;
        pthread_mutex_lock(&s->workers[target].deque_lock);
        deque_push_bottom(&s->workers[target].deque, g);
        pthread_mutex_unlock(&s->workers[target].deque_lock);
    } else {
        pthread_mutex_lock(&s->global_deque_lock);
        deque_push_bottom(&s->global_deque, g);
        pthread_mutex_unlock(&s->global_deque_lock);
        for (int i = 0; i < s->worker_count; i++) {
            worker_stealing_t* w = &s->workers[i];
            if (atomic_load(&w->is_idle)) {
                pthread_mutex_lock(&w->lock);
                pthread_cond_signal(&w->cond);
                pthread_mutex_unlock(&w->lock);
            }
        }
    }

    printf("Added goroutine %d to scheduler queue (active: %d)\n",
           goroutine_id, atomic_load(&s->active_goroutines));
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

    int last_count = atomic_load(&s->active_goroutines);
    while (atomic_load(&s->active_goroutines) > 0) {
        int current = atomic_load(&s->active_goroutines);
        if (current != last_count) {
            printf("  %d goroutines remaining...\n", current);
            last_count = current;
        }
        usleep(500000);  // 500ms
    }
    printf("All goroutines completed successfully!\n");

    atomic_store(&s->running, 0);

    for (int i = 0; i < s->worker_count; i++) {
        pthread_cond_broadcast(&s->workers[i].cond);
        pthread_join(s->workers[i].thread, NULL);
    }

    // Print statistics
    printf("\n=== Work-Stealing Statistics ===\n");
    int total = 0;
    for (int i = 0; i < s->worker_count; i++) {
        int exec = atomic_load(&s->workers[i].goroutines_executed);
        total += exec;
        printf("Worker %d: %d goroutines executed, %d steals done, %d steals received\n",
               i, exec,
               atomic_load(&s->workers[i].steals_done),
               atomic_load(&s->workers[i].steals_received));
    }
    printf("Total executed: %d goroutines\n", total);
}





void scheduler_stealing_destroy(scheduler_stealing_t* s) {
    if (!s) return;

    // Free all workers' deques
    for (int i = 0; i < s->worker_count; i++) {
        deque_destroy(&s->workers[i].deque);
        pthread_mutex_destroy(&s->workers[i].deque_lock);
        pthread_mutex_destroy(&s->workers[i].lock);
        pthread_cond_destroy(&s->workers[i].cond);
    }
    
    // Free global deque
    deque_destroy(&s->global_deque);
    pthread_mutex_destroy(&s->global_deque_lock);
    
    // Free workers array
    free(s->workers);
    
    printf("Scheduler destroyed\n");
}

static void* worker_stealing_loop(void* arg) {
    worker_stealing_t* w = (worker_stealing_t*)arg;
    scheduler_stealing_t* s = w->scheduler;

    if (goroutine_thread_init() != 0) {
        perror("goroutine_thread_init");
        atomic_store(&s->running, 0);
        return NULL;
    }
    
    printf("Worker %d running on CPU %d\n", w->id, sched_getcpu());
    
    while (atomic_load(&s->running)) {
        goroutine_t* g = NULL;
        
        // 1. Try to pop from local deque
        pthread_mutex_lock(&w->deque_lock);
        g = deque_pop_bottom(&w->deque);
        pthread_mutex_unlock(&w->deque_lock);
        
        // 2. If local empty, try to steal
        if (!g && atomic_load(&s->active_goroutines) > 0) {
            g = try_steal(w);
        }
        
        // 3. If still empty, try global queue
        if (!g && atomic_load(&s->active_goroutines) > 0) {
            pthread_mutex_lock(&s->global_deque_lock);
            g = deque_steal(&s->global_deque);
            pthread_mutex_unlock(&s->global_deque_lock);
        }
        
        // 4. Execute if we found work
        if (g) {
            execute_goroutine_stealing(w, g);
            sched_yield();
        } else {
            // No work → brief sleep + cond wait
            atomic_store(&w->is_idle, 1);
            pthread_mutex_lock(&w->lock);
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += 50000000;
            if (ts.tv_nsec >= 1000000000) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000;
            }
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
    if (s->worker_count < 2) return NULL;
    const int MAX_STEAL_ATTEMPTS = 3;
    
    for (int attempt = 0; attempt < MAX_STEAL_ATTEMPTS; attempt++) {
        // Pick random victim (avoid stealing from self)
        int victim_id;
        do {
            victim_id = rand() % s->worker_count;
        } while (victim_id == thief->id);
        
        worker_stealing_t* victim = &s->workers[victim_id];
        
        // A started ucontext stays on its original pthread when resumed.
        pthread_mutex_lock(&victim->deque_lock);
        size_t top = atomic_load(&victim->deque.top);
        goroutine_t* candidate = top < victim->deque.bottom
                                     ? victim->deque.buffer[top & victim->deque.mask]
                                     : NULL;
        goroutine_t* g = candidate && candidate->worker_id == -1
                             ? deque_steal(&victim->deque)
                             : NULL;
        pthread_mutex_unlock(&victim->deque_lock);
        
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

static void execute_goroutine_stealing(worker_stealing_t* w, goroutine_t* g) {
    int expected = GOROUTINE_READY;
    if (!atomic_compare_exchange_strong(&g->state, &expected, GOROUTINE_RUNNING)) return;
    if (g->worker_id == -1) g->worker_id = w->id;
    printf("Worker %d: Executing goroutine %d\n", w->id, g->id);

    ucontext_t worker_context;
    g->return_context = &worker_context;
    g->context.uc_link = &worker_context;
    current_goroutine = g;
    goroutine_set_active(g);
    current_worker_context = &worker_context;

    if (swapcontext(&worker_context, &g->context) == -1) {
        perror("swapcontext");
        current_goroutine = NULL;
        goroutine_set_active(NULL);
        current_worker_context = NULL;
        atomic_store(&g->state, GOROUTINE_FINISHED);
        atomic_fetch_sub(&w->scheduler->active_goroutines, 1);
        free_goroutine(g);
        return;
    }

    current_goroutine = NULL;
    goroutine_set_active(NULL);
    current_worker_context = NULL;

    int state = atomic_load(&g->state);
    if (state == GOROUTINE_PARKING) {
        int expected = GOROUTINE_PARKING;
        atomic_compare_exchange_strong(&g->state, &expected, GOROUTINE_BLOCKED);
        state = atomic_load(&g->state);
    }

    if (state == GOROUTINE_WAKE_PENDING) {
        int expected = GOROUTINE_WAKE_PENDING;
        if (atomic_compare_exchange_strong(&g->state, &expected, GOROUTINE_READY)) {
            int target = g->worker_id >= 0
                             ? g->worker_id
                             : atomic_fetch_add(&w->scheduler->next_worker, 1) %
                                   w->scheduler->worker_count;
            worker_stealing_t* worker = &w->scheduler->workers[target];
            pthread_mutex_lock(&worker->deque_lock);
            deque_push_bottom(&worker->deque, g);
            pthread_mutex_unlock(&worker->deque_lock);
            return;
        }
        state = atomic_load(&g->state);
    }

    if (state == GOROUTINE_YIELDING) {
        atomic_store(&g->state, GOROUTINE_READY);
        pthread_mutex_lock(&w->deque_lock);
        deque_push_bottom(&w->deque, g);
        pthread_mutex_unlock(&w->deque_lock);
        return;
    }

    if (state == GOROUTINE_BLOCKED) return;
    if (state == GOROUTINE_FINISHED) {
        printf("Worker %d: Goroutine %d completed\n", w->id, g->id);
        atomic_fetch_add(&w->goroutines_executed, 1);
        atomic_fetch_sub(&w->scheduler->active_goroutines, 1);
        free_goroutine(g);
    }
}

goroutine_t* get_current_goroutine(void) {
    return current_goroutine;
}

int scheduler_yield_current(void) {
    goroutine_t* g = current_goroutine;
    ucontext_t* worker_context = current_worker_context;
    if (!g || !worker_context) return -1;

    int expected = GOROUTINE_RUNNING;
    if (!atomic_compare_exchange_strong(&g->state, &expected, GOROUTINE_YIELDING)) return -1;
    if (swapcontext(&g->context, worker_context) == -1) {
        atomic_store(&g->state, GOROUTINE_RUNNING);
        return -1;
    }
    return 0;
}

void park_goroutine(void) {
    goroutine_t* g = current_goroutine;
    if (!g || !current_worker_context) {
        fprintf(stderr, "park_goroutine called outside a running goroutine\n");
        return;
    }

    int expected = GOROUTINE_RUNNING;
    if (!atomic_compare_exchange_strong(&g->state, &expected, GOROUTINE_PARKING)) {
        if (expected == GOROUTINE_WAKE_PENDING) {
            atomic_store(&g->state, GOROUTINE_RUNNING);
            return;
        }
        fprintf(stderr, "goroutine %d cannot park from state %d\n", g->id, expected);
        return;
    }

    // Save this goroutine's continuation, then return to the worker; a later
    // scheduler dispatch resumes it at this swapcontext call.
    if (swapcontext(&g->context, current_worker_context) == -1) {
        perror("swapcontext while parking goroutine");
        atomic_store(&g->state, GOROUTINE_RUNNING);
    }
}

void scheduler_ready(goroutine_t* g) {
    if (!g || !g->scheduler) return;
    scheduler_stealing_t* s = g->scheduler;

    int state = atomic_load(&g->state);
    if (state == GOROUTINE_RUNNING || state == GOROUTINE_PARKING) {
        int expected = state;
        if (atomic_compare_exchange_strong(&g->state, &expected, GOROUTINE_WAKE_PENDING)) return;
        state = expected;
        if (state == GOROUTINE_PARKING) {
            expected = GOROUTINE_PARKING;
            if (atomic_compare_exchange_strong(&g->state, &expected, GOROUTINE_WAKE_PENDING)) return;
            state = expected;
        }
    }

    if (state != GOROUTINE_BLOCKED) return;
    int expected = GOROUTINE_BLOCKED;
    if (!atomic_compare_exchange_strong(&g->state, &expected, GOROUTINE_READY)) return;

    int target = g->worker_id >= 0
                     ? g->worker_id
                     : atomic_fetch_add(&s->next_worker, 1) % s->worker_count;
    worker_stealing_t* worker = &s->workers[target];
    pthread_mutex_lock(&worker->deque_lock);
    deque_push_bottom(&worker->deque, g);
    pthread_mutex_unlock(&worker->deque_lock);
}
