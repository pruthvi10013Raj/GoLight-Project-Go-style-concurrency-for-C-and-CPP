#ifndef GOROUTINE_H
#define GOROUTINE_H

#include <ucontext.h>
#include <stddef.h>
#include <stdatomic.h>

// Goroutine state constants
#define GOROUTINE_READY    0
#define GOROUTINE_RUNNING  1
#define GOROUTINE_FINISHED 2
#define GOROUTINE_BLOCKED  3
#define GOROUTINE_PARKING  4
#define GOROUTINE_WAKE_PENDING 5
#define GOROUTINE_YIELDING 6

// Simple goroutine structure
typedef struct goroutine {
    ucontext_t context;
    void *stack;
    size_t stack_size;
    void *stack_mapping;
    size_t stack_mapping_size;
    size_t guard_page_size;
    int id;
    int worker_id;
    atomic_int state;
    struct scheduler_stealing *scheduler;
    ucontext_t *return_context;
    struct goroutine *wait_next;
    void *wait_channel;
    void *wait_value;
    int wait_value_ready;
    void (*function)(void*);
    void *arg;
} goroutine_t;

// Function declarations
goroutine_t* create_goroutine(void (*func)(void*), void *arg);
void run_goroutine(goroutine_t *g);
void free_goroutine(goroutine_t *g);
int goroutine_thread_init(void);
void goroutine_set_active(goroutine_t *g);

#endif
