#include "golight.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#define CHANNEL_TASKS 100

static channel_t* channel;
static int sender_ids[CHANNEL_TASKS];
static atomic_int senders_done;
static atomic_int receivers_done;

static void sender(void* arg) {
    int* id = arg;
    chan_send(channel, id);
    printf("Sender %d: done\n", *id);
    atomic_fetch_add(&senders_done, 1);
}

static void receiver(void* arg) {
    (void)arg;
    int* id = chan_receive(channel);
    if (!id) return;
    printf("Receiver got %d\n", *id);
    atomic_fetch_add(&receivers_done, 1);
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

    for (int i = 0; i < CHANNEL_TASKS; i++) {
        sender_ids[i] = i;
        if (go(sender, &sender_ids[i]) != 0) {
            perror("go sender");
            return EXIT_FAILURE;
        }
    }
    for (int i = 0; i < CHANNEL_TASKS; i++) {
        if (go(receiver, NULL) != 0) {
            perror("go receiver");
            return EXIT_FAILURE;
        }
    }

    runtime_wait();
    int send_count = atomic_load(&senders_done);
    int receive_count = atomic_load(&receivers_done);
    printf("Channel integration complete: %d senders, %d receivers\n",
           send_count, receive_count);
    runtime_shutdown();
    return send_count == CHANNEL_TASKS && receive_count == CHANNEL_TASKS
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
