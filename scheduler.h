#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "goroutine.h"

#define MAX_GOROUTINES 100

typedef struct scheduler {
	goroutine_t *goroutines[MAX_GOROUTINES];
	int count;
	int current;
	ucontext_t main_context;
} scheduler_t;

//these will be our scheduler functions
void scheduler_init(scheduler_t *s);
void scheduler_add(scheduler_t *s, goroutine_t *g);
void scheduler_run(scheduler_t *s);
void scheduler_yield(scheduler_t *s);

