#include "scheduler.h"
#include <stdio.h>
#include <stdlib.h>

void scheduler_init(scheduler_t *s) {
	s->count = 0;
	s->current = -1;
	for (int i = 0; i< MAX_GOROUTINES; i++) {
		s->goroutines[i] = NULL;
	}
}

void scheduler_add(scheduler_t *s, goroutine_t *g){
	if (s->count < MAX_GOROUTINES) {
		s->goroutines[s->count] = g;
		s->count++;
	}
}

//creating a simple round roubin scheduler

void scheduler_run(scheduler_t *s){
	if (s->count == 0) return;

	getcontext(&s->main-context);//saving the main context

	
	for(int i = 0; i < s->count; i++){
		s->current = i;
		goroutine_t *g = s->goroutines[i];

		if(g->state == 0) {
			//present state = runnig state
			printf("\n Scheduler: Switching to goroutind\n", g->id);
			g->state = 1;// state set to running

			//setting where to go when done
			g->context.uc_link = &s->main_context);

			//switch to goroutine
			swapcontext(&s->main_context, &g->context);

			//now to finish goroutine
			g->state = 2;
			printf("Scheduler : Goroutine %d finished\n", g->id);
		}
	}
	
	printf("\nScheduler : All goroutines completed\n");
}		//Simple round robin scheduler runs each goroutine once 


//for the sake of now, simple implenmentation used to yeild
void scheduler_yield(scheduler_t *s) {
	setcontext(&s->main_context);
}

