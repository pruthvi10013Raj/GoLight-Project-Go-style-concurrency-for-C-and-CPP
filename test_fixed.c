#include "worker_stealing.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

void test_function(void* arg) {
    int id = *(int*)arg;
    free(arg);
    
    printf("  Goroutine %d: STARTING work\n", id);
    
    // Simulate some work
    volatile int sum = 0;
    for (int i = 0; i < 1000000; i++) {
        sum += i;
    }
    
    printf("  Goroutine %d: FINISHED (sum=%d)\n", id, sum);
}

int main() {
    printf("=== Testing Fixed Work-Stealing Scheduler ===\n\n");
    
    scheduler_stealing_t s;
    
    if (scheduler_stealing_init(&s, 2) != 0) {
        printf("Failed to initialize scheduler\n");
        return 1;
    }
    
    // Create 5 goroutines
    printf("Creating 5 goroutines...\n");
    for (int i = 1; i <= 5; i++) {
        int* id = malloc(sizeof(int));
        *id = i;
        goroutine_t* g = create_goroutine(test_function, id);
        scheduler_stealing_add(&s, g);
    }
    
    printf("\nStarting scheduler...\n");
    scheduler_stealing_start(&s);
    
    printf("Waiting for completion...\n");
    scheduler_stealing_wait(&s);
    
    scheduler_stealing_destroy(&s);
    
    printf("\n=== Test Complete ===\n");
    return 0;
}
