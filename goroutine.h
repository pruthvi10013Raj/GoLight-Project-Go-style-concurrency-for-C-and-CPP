#ifndef GOROUTINE_H
#define GOROUTINE_H

#include <ucontext.h>
#include <stddef.h>

// Goroutine state constants
#define GOROUTINE_READY    0
#define GOROUTINE_RUNNING  1
#define GOROUTINE_FINISHED 2

// Simple goroutine structure
typedef struct goroutine {
    ucontext_t context;
    void *stack;
    size_t stack_size;
    int id;
    int state;  // 0: ready, 1: running, 2: finished
    void (*function)(void*);
    void *arg;
} goroutine_t;

// Function declarations
goroutine_t* create_goroutine(void (*func)(void*), void *arg);
void run_goroutine(goroutine_t *g);
void free_goroutine(goroutine_t *g);

#endif
