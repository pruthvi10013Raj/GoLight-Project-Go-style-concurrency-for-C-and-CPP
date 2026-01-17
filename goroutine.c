#include "goroutine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int next_id = 1;

static void goroutine_wrapper(goroutine_t *g) {
//the function that actually runs the goroutine
	g->function(g->arg);
	g->state =2; //as we defined state 2 for completed state, in goroutine.h
}

goroutine_t* create_goroutine(void (*func)(void*), void *arg) {
	goroutine_t *g = malloc(sizeof(goroutine_t));
	if (!g) return NULL;

 	g->id = next_id++;
	g->function = func;
	g->arg = arg;
	g->state = 0;//i.e. ready state
	g->stacj_size = 8192;
	g->stack = malloc(g->stack_size);

	//inititalizing context
	getcontext(&g->context);
	g->context.uc_stack.ss_sp = g->stack;
	g->context.uc_stack.ss_size = g->stack_size;
	g->context.uc_link = NULL

	//creating the context
	makecontext(&g->context, (void(*)(void))goroutine_wrapper, 1, g);

	printf("Created goroutine %d\n", g->id);
	return g;
}

void run_goroutine(goroutine_t *g) {
	if (g->state ==0){       //this function will only run if in ready state
		g->state = 1; //set state to running
		printf("Starting goroutine %d\n", g->id);
		setcontext(&g->context);
	}
}

void free_goroutine(goroutine_t *g){
	free(g->stack);
	free(g);
}

