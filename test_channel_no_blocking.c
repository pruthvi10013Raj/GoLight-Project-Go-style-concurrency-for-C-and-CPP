#include "golight.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

static channel_t* channel;
static atomic_int printer_finished;
static atomic_int sender_started;
static atomic_int receiver_unblocked_after_printer;

static void blocked_receiver(void* arg) {
    (void)arg;
    void* value = chan_receive(channel);
    if (value) {
        atomic_store(&receiver_unblocked_after_printer,
                     atomic_load(&printer_finished));
        printf("Receiver unblocked\n");
    }
}

static void yielding_printer(void* arg) {
    (void)arg;
    for (int i = 1; i <= 10; i++) {
        if (atomic_load(&sender_started)) {
            fprintf(stderr, "Printer ran after delayed sender started\n");
            exit(EXIT_FAILURE);
        }
        printf("I'm running while channel is blocked! %d\n", i);
        yield();
    }
    atomic_store(&printer_finished, 1);
}

static void delayed_sender(void* arg) {
    (void)arg;
    future_t* timer = async_sleep(500);
    if (!timer) {
        perror("async_sleep");
        exit(EXIT_FAILURE);
    }
    (void)future_wait(timer);
    future_free(timer);
    atomic_store(&sender_started, 1);
    printf("Delayed sender: unblocking channel\n");
    chan_send(channel, (void*)1);
}

int main(void) {
    if (runtime_init() != 0) {
        perror("runtime_init");
        return EXIT_FAILURE;
    }
    channel = make_channel(0);
    if (!channel) {
        perror("make_channel");
        runtime_shutdown();
        return EXIT_FAILURE;
    }

    if (go(blocked_receiver, NULL) != 0 ||
        go(yielding_printer, NULL) != 0 ||
        go(delayed_sender, NULL) != 0) {
        perror("go");
        return EXIT_FAILURE;
    }

    runtime_wait();
    int passed = atomic_load(&printer_finished) &&
                 atomic_load(&receiver_unblocked_after_printer);
    runtime_shutdown();
    printf("Channel nonblocking test: %s\n", passed ? "PASS" : "FAIL");
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
