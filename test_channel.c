#include "goroutine.h"
#include "scheduler.h"
#include "channel.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

channel_t *global_channel;

void producer(void *arg) {
    int id = *(int*)arg;
    
    char message[100];
    sprintf(message, "Message from producer %d", id);
    
    printf("Producer %d: Sending '%s'\n", id, message);
    channel_send(global_channel, strdup(message));  // strdup copies string
    
    printf("Producer %d: Sent!\n", id);
}

void consumer(void *arg) {
    int id = *(int*)arg;
    
    printf("Consumer %d: Waiting for message...\n", id);
    char *message = channel_receive(global_channel);
    
    printf("Consumer %d: Received '%s'\n", id, message);
    
    free(message);  // Free the duplicated string
}

int main() {
    printf("=== Testing Channels ===\n");
    
    // Create channel
    global_channel = channel_create(5);
    
    scheduler_t scheduler;
    scheduler_init(&scheduler);
    
    // Create producers and consumers
    int p_ids[] = {1, 2, 3};
    int c_ids[] = {1, 2};
    
    // Add producers
    for (int i = 0; i < 3; i++) {
        goroutine_t *g = create_goroutine(producer, &p_ids[i]);
        scheduler_add(&scheduler, g);
    }
    
    // Add consumers
    for (int i = 0; i < 2; i++) {
        goroutine_t *g = create_goroutine(consumer, &c_ids[i]);
        scheduler_add(&scheduler, g);
    }
    
    // Run everything
    scheduler_run(&scheduler);
    
    // Cleanup
    channel_destroy(global_channel);
    
    printf("\n=== Channel Test Complete ===\n");
    return 0;
}
