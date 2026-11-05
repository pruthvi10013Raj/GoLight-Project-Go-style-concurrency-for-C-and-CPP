#include "golight.h"

#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define PRODUCER_COUNT 10
#define CONSUMER_COUNT 10
#define IO_GOROUTINE_COUNT 5
#define ITEMS_PER_GOROUTINE 10
#define TOTAL_ITEMS (PRODUCER_COUNT * ITEMS_PER_GOROUTINE)

static channel_t* items_channel;
static waitgroup_t tasks;
static int items[TOTAL_ITEMS];
static atomic_int sent_count;
static atomic_int received_count;
static atomic_int io_count;
static atomic_int failure_count;

static void producer(void* arg) {
    int producer_id = *(int*)arg;
    int first_item = producer_id * ITEMS_PER_GOROUTINE;

    for (int i = 0; i < ITEMS_PER_GOROUTINE; i++) {
        int* item = &items[first_item + i];
        chan_send(items_channel, item);
        atomic_fetch_add(&sent_count, 1);
    }

    waitgroup_done(tasks);
}

static void consumer(void* arg) {
    (void)arg;
    for (int i = 0; i < ITEMS_PER_GOROUTINE; i++) {
        int* item = chan_receive(items_channel);
        if (!item) {
            atomic_fetch_add(&failure_count, 1);
            continue;
        }
        printf("Received item %d\n", *item);
        atomic_fetch_add(&received_count, 1);
    }
    waitgroup_done(tasks);
}

static void io_worker(void* arg) {
    int worker_id = *(int*)arg;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd == -1) {
        perror("open /dev/urandom");
        atomic_fetch_add(&failure_count, 1);
        waitgroup_done(tasks);
        return;
    }

    for (int i = 0; i < ITEMS_PER_GOROUTINE; i++) {
        unsigned char byte;
        future_t* future = async_read(fd, &byte, sizeof(byte));
        if (!future) {
            fprintf(stderr, "I/O goroutine %d: async_read failed\n", worker_id);
            atomic_fetch_add(&failure_count, 1);
            continue;
        }
        int result = future_wait(future);
        future_free(future);
        if (result != 1) {
            fprintf(stderr, "I/O goroutine %d: read returned %d\n",
                    worker_id, result);
            atomic_fetch_add(&failure_count, 1);
            continue;
        }
        atomic_fetch_add(&io_count, 1);
    }

    close(fd);
    waitgroup_done(tasks);
}

int main(void) {
    if (runtime_init() != 0) {
        perror("runtime_init");
        return EXIT_FAILURE;
    }

    items_channel = make_channel(0);
    tasks = waitgroup_create();
    if (!items_channel || !tasks) {
        perror("channel or waitgroup creation");
        runtime_shutdown();
        return EXIT_FAILURE;
    }

    waitgroup_add(tasks, PRODUCER_COUNT + CONSUMER_COUNT + IO_GOROUTINE_COUNT);

    int producer_ids[PRODUCER_COUNT];
    int io_ids[IO_GOROUTINE_COUNT];
    for (int i = 0; i < PRODUCER_COUNT; i++) {
        producer_ids[i] = i;
        if (go(producer, &producer_ids[i]) != 0) {
            perror("go producer");
            atomic_fetch_add(&failure_count, 1);
            waitgroup_done(tasks);
        }
    }
    for (int i = 0; i < CONSUMER_COUNT; i++) {
        if (go(consumer, NULL) != 0) {
            perror("go consumer");
            atomic_fetch_add(&failure_count, 1);
            waitgroup_done(tasks);
        }
    }
    for (int i = 0; i < IO_GOROUTINE_COUNT; i++) {
        io_ids[i] = i;
        if (go(io_worker, &io_ids[i]) != 0) {
            perror("go I/O worker");
            atomic_fetch_add(&failure_count, 1);
            waitgroup_done(tasks);
        }
    }

    waitgroup_wait(tasks);

    int sent = atomic_load(&sent_count);
    int received = atomic_load(&received_count);
    int io_completed = atomic_load(&io_count);
    printf("Total items sent: %d\n", sent);
    printf("Total items received: %d\n", received);
    printf("I/O ops completed: %d\n", io_completed);
    int passed = sent == TOTAL_ITEMS && received == TOTAL_ITEMS &&
                 io_completed == IO_GOROUTINE_COUNT * ITEMS_PER_GOROUTINE &&
                 atomic_load(&failure_count) == 0;
    printf("Full integration test %s\n", passed ? "PASSED" : "FAILED");

    waitgroup_destroy(tasks);
    chan_close(items_channel);
    runtime_shutdown();
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
