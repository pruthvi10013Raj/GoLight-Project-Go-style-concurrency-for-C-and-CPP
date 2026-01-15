#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>

ucontext_t main_context, child_context;

void child_function(){
	printf("Child:Hello from child context!\n");
	printf("Child: switching back to main...\n");
	setcontext(&main_context);
}

int main(){
	printf("main: starting...\n");
	
	char *child_stack = malloc(8192);
	getcontext(&child_context);

	child_context.uc_stack.ss_sp = child_stack;
	child_context.uc_stack.ss_size=8192;
	child_context.uc_link = &main_context;

	makecontext(&child_context, child_function, 0);

	printf("main: switching to child...\n");

	swapcontext(&main_context, &child_context);

	printf("main: back from child\n");

	free(child_stack);
	return 0;

}

