#include "goroutine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int next_id =1;

//this will be helper function which will actually runs the goroutine
static void goroutine_wrapper(goroutine_t *g) {
	g->function(g->arg);
	g->state = 2;   //as we mentioned in goroutine.h that state=2 for finished state
}

goroutine_t* create_goroutine(void (*func)(void*),void *arg) {
	goroutine_t *g = malloc(sizeof(goroutine_t));
	if (!g) return NULL;

	g->id = next_id++;
	g->function = func;
	g->arg = arg;
	g->stack = 0; //0 for ready state
	g->stack_size = 8192;
	g->stack = malloc(g->stack_size);

	//context initialization
	getcontext(&g->context);
	g->context.uc_stack.ss_sp = g->stack;
	g->context.uc_stack.ss_size = g->stack_size;
