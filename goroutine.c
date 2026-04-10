#include "goroutine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int next_id = 1;

static void goroutine_wrapper(goroutine_t *g) {
    // Run the user's function
    g->function(g->arg);
    
    // Mark as finished
    g->state = GOROUTINE_FINISHED;
    
    // IMPORTANT: Return to scheduler
    // The uc_link we set in execute_goroutine_stealing will take us back
    // Just return - the context will automatically go to uc_link
}

goroutine_t* create_goroutine(void (*func)(void*), void *arg) {
    goroutine_t *g = malloc(sizeof(goroutine_t));
    if (!g) return NULL;
    
    g->id = next_id++;
    g->function = func;
    g->arg = arg;
    g->state = GOROUTINE_READY;
    g->stack_size = 8192;
    g->stack = malloc(g->stack_size);
    
    if (!g->stack) {
        free(g);
        return NULL;
    }
    
    // Initialize context
    getcontext(&g->context);
    g->context.uc_stack.ss_sp = g->stack;
    g->context.uc_stack.ss_size = g->stack_size;
    g->context.uc_link = NULL;  // Will be set by scheduler when running
    
    // Create context that calls wrapper with goroutine as argument
    makecontext(&g->context, (void(*)(void))goroutine_wrapper, 1, g);
    
    printf("Created goroutine %d\n", g->id);
    return g;
}

void run_goroutine(goroutine_t *g) {
    if (g->state == GOROUTINE_READY) {
        g->state = GOROUTINE_RUNNING;
        printf("Starting goroutine %d\n", g->id);
        setcontext(&g->context);
    }
}

void free_goroutine(goroutine_t *g){
	if (g) {
    	    if (g->stack) free(g->stack);
            free(g);
       }
}
