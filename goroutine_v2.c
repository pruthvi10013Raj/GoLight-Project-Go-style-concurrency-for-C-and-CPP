#include "goroutine.h"
#include "scheduler.h"
#include <stdio.h>
#include <stdlib.h>

// We need a global scheduler for now
// In real implementation, this would be thread-local
extern scheduler_t *global_scheduler;

static int next_id = 1;

// Updated wrapper
static void goroutine_wrapper_v2(goroutine_t *g) {
    g->function(g->arg);
    g->state = 2;  // Finished
    // Return to scheduler
    // In real implementation, we'd call scheduler_yield
}

goroutine_t* create_goroutine(void (*func)(void*), void *arg) {
    goroutine_t *g = malloc(sizeof(goroutine_t));
    if (!g) return NULL;
    
    g->id = next_id++;
    g->function = func;
    g->arg = arg;
    g->state = 0;  // Ready
    g->stack_size = 8192;
    g->stack = malloc(g->stack_size);
    
    getcontext(&g->context);
    g->context.uc_stack.ss_sp = g->stack;
    g->context.uc_stack.ss_size = g->stack_size;
    g->context.uc_link = NULL;
    
    makecontext(&g->context, (void (*)(void))goroutine_wrapper_v2, 1, g);
    
    printf("Created goroutine %d\n", g->id);
    return g;
}

void run_goroutine(goroutine_t *g) {
    if (g->state == 0) {  // Only run if ready
        g->state = 1;  // Running
        printf("Starting goroutine %d\n", g->id);
        setcontext(&g->context);
    }
}

void free_goroutine(goroutine_t *g) {
    free(g->stack);
    free(g);
}
