#include "worker_stealing.h"

#include <stdio.h>
#include <stdlib.h>

__attribute__((noinline))
static void recurse(int depth) {
    volatile unsigned char frame[128];
    frame[0] = (unsigned char)depth;
    frame[sizeof(frame) - 1] = (unsigned char)(depth >> 8);
    if (depth > 0) recurse(depth - 1);
    if (frame[0] == 255) puts("unreachable");
}

static void overflow_task(void* arg) {
    (void)arg;
    recurse(10000);
}

int main(void) {
    scheduler_stealing_t scheduler;
    if (scheduler_stealing_init(&scheduler, 1) != 0) {
        perror("scheduler_stealing_init");
        return EXIT_FAILURE;
    }

    goroutine_t* g = create_goroutine(overflow_task, NULL);
    if (!g) {
        perror("create_goroutine");
        scheduler_stealing_destroy(&scheduler);
        return EXIT_FAILURE;
    }
    scheduler_stealing_add(&scheduler, g);
    scheduler_stealing_start(&scheduler);
    scheduler_stealing_wait(&scheduler);
    scheduler_stealing_destroy(&scheduler);

    fputs("ERROR: recursion returned without a stack overflow\n", stderr);
    return EXIT_FAILURE;
}
