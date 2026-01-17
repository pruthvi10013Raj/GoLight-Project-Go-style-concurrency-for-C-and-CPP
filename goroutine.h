#ifndef GOROUTINE_H
#define GOROUTINE_H

#include <ucontext.h>
#include <stddef.h>

//a simple goroutine structure
typedef struct goroutine {
	ucontext_t context;
	void *stack;
	size_t stack_size;
	int id;
	int state; //0for ready, lly 1:runnig, 2:finished
	void (*function)(void*);
	void *arg;
} goroutine_t;

//function declarations as below
goroutine_t* create_goroutine(void (*func)(void*), void *arg);
void run_goroutine(goroutine_t *g);
void free_goroutne(goroutine_t *g);

#endif
