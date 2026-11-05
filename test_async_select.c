#include "golight.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static int ready_pipe[2];
static int slow_pipe[2];
static atomic_int tick_count;
static atomic_int select_returned;
static atomic_int worker_test_passed;

static int64_t elapsed_milliseconds(struct timespec start, struct timespec end) {
    return (int64_t)(end.tv_sec - start.tv_sec) * 1000 +
           (end.tv_nsec - start.tv_nsec) / 1000000;
}

static void delayed_write(void* arg) {
    int fd = *(int*)arg;
    future_t* timer = async_sleep(200);
    if (!timer) {
        perror("async_sleep");
        return;
    }
    (void)future_wait(timer);
    future_free(timer);
    if (write(fd, "r", 1) != 1) perror("write ready pipe");
}

static void tick_worker(void* arg) {
    (void)arg;
    for (int i = 0; i < 5; i++) {
        printf("tick %d\n", i + 1);
        atomic_fetch_add(&tick_count, 1);
        yield();
    }
}

static void slow_writer(void* arg) {
    int fd = *(int*)arg;
    future_t* timer = async_sleep(1000);
    if (!timer) {
        perror("async_sleep");
        return;
    }
    (void)future_wait(timer);
    future_free(timer);
    if (write(fd, "s", 1) != 1) perror("write slow pipe");
}

static void selecting_worker(void* arg) {
    int fd = *(int*)arg;
    async_select_case_t select_case = {
        .fd = fd,
        .type = ASYNC_WAIT_READ,
        .future = NULL,
    };
    int result = async_select(&select_case, 1, 5000);
    int passed = result == 0 && atomic_load(&tick_count) == 5;
    atomic_store(&worker_test_passed, passed);
    atomic_store(&select_returned, 1);
    printf("Worker select result: %d; ticks before return: %d\n",
           result, atomic_load(&tick_count));
}

int main(void) {
    if (pipe(ready_pipe) == -1 || runtime_init() != 0) {
        perror("pipe/runtime_init");
        return EXIT_FAILURE;
    }

    if (go(delayed_write, &ready_pipe[1]) != 0) {
        perror("go delayed writer");
        runtime_shutdown();
        return EXIT_FAILURE;
    }

    int idle_pipe[2];
    if (pipe(idle_pipe) == -1) {
        perror("pipe idle");
        runtime_shutdown();
        return EXIT_FAILURE;
    }
    async_select_case_t readiness_cases[2] = {
        {.fd = idle_pipe[0], .type = ASYNC_WAIT_READ, .future = NULL},
        {.fd = ready_pipe[0], .type = ASYNC_WAIT_READ, .future = NULL},
    };
    int ready = async_select(readiness_cases, 2, 5000);
    printf("Ready index: %d (expected: 1)\n", ready);
    int readiness_passed = ready == 1;
    close(idle_pipe[0]);
    close(idle_pipe[1]);
    close(ready_pipe[0]);
    close(ready_pipe[1]);

    int timeout_pipe[2];
    if (pipe(timeout_pipe) == -1) {
        perror("pipe timeout");
        runtime_shutdown();
        return EXIT_FAILURE;
    }
    async_select_case_t timeout_case = {
        .fd = timeout_pipe[0],
        .type = ASYNC_WAIT_READ,
        .future = NULL,
    };
    struct timespec start;
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    int timeout_result = async_select(&timeout_case, 1, 500);
    clock_gettime(CLOCK_MONOTONIC, &end);
    int64_t elapsed = elapsed_milliseconds(start, end);
    printf("Timeout result: %d after %llu ms (expected: -1)\n",
           timeout_result, (unsigned long long)elapsed);
    int timeout_passed = timeout_result == -1 && elapsed >= 450 && elapsed < 2000;
    close(timeout_pipe[0]);
    close(timeout_pipe[1]);

    if (pipe(slow_pipe) == -1) {
        perror("pipe slow");
        runtime_shutdown();
        return EXIT_FAILURE;
    }
    if (go(tick_worker, NULL) != 0 ||
        go(slow_writer, &slow_pipe[1]) != 0 ||
        go(selecting_worker, &slow_pipe[0]) != 0) {
        perror("go worker select test");
        runtime_shutdown();
        return EXIT_FAILURE;
    }
    runtime_wait();
    printf("Worker nonblocking test: %s\n",
           atomic_load(&worker_test_passed) ? "PASS" : "FAIL");
    int worker_passed = atomic_load(&worker_test_passed) &&
                        atomic_load(&select_returned);
    close(slow_pipe[0]);
    close(slow_pipe[1]);
    runtime_shutdown();

    int passed = readiness_passed && timeout_passed && worker_passed;
    printf("Async select tests %s\n", passed ? "PASSED" : "FAILED");
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
