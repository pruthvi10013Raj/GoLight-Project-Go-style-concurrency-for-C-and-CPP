#include "goroutine_lib.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

// Forward declarations
void send_messages(void *arg);
void receive_messages(void *arg);
void channel_demo(void *arg);


void print_hello(void *arg) {
    char *name = (char*)arg;
    printf("Hello from goroutine: %s\n", name);
}

void channel_demo(void *arg) {
    channel_t ch = make_channel(3);

    // Simple producer-consumer
    go(send_messages, ch);
    go(receive_messages, ch);

    // Wait a bit
    sleep(1);
}

void send_messages(void *arg) {
    channel_t ch = (channel_t)arg;
    for (int i = 0; i < 5; i++) {
        char msg[50];
        sprintf(msg, "Message %d", i);
        printf("Sending: %s\n", msg);
        chan_send(ch, strdup(msg));
        usleep(100000);  // Sleep 100ms
    }
    chan_close(ch);
}

void receive_messages(void *arg) {
    channel_t ch = (channel_t)arg;
    void *msg;
    while ((msg = chan_receive(ch)) != NULL) {
        printf("Received: %s\n", (char*)msg);
        free(msg);
    }
    printf("Channel closed\n");
}


int main() {
    printf("=== Goroutine Library Demo ===\n\n");
    
    // Initialize runtime
    goroutine_init();
    
    // Example 1: Simple goroutines
    printf("1. Simple Goroutines:\n");
    go(print_hello, "Alice");
    go(print_hello, "Bob");
    go(print_hello, "Charlie");
    
    // Give time for goroutines to run
    sleep(1);
    
    printf("\n2. Channels Demo:\n");
    channel_demo(NULL);
    
    printf("\n=== Demo Complete ===\n");
    return 0;
}
