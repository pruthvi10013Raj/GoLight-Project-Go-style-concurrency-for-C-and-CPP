#include "worker_stealing.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

void test_function(void* arg) {
    int id = *(int*)arg;
    free(arg);
    
    printf("  ▶ Goroutine %d: STARTING\n", id);
    
    // Simulate some work
    volatile long sum = 0;
    for (long i = 0; i < 5000000; i++) {
        sum += i;
    }
    
    printf("  ✓ Goroutine %d: FINISHED (sum=%ld)\n", id, sum);
}

int main() {
    printf("╔══════════════════════════════════════════╗\n");
    printf("║  Testing Fixed Work-Stealing Scheduler  ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");
    
    scheduler_stealing_t s;
    
    if (scheduler_stealing_init(&s, 2) != 0) {
        printf("Failed to initialize scheduler\n");
        return 1;
    }
    
    // Create 3 goroutines only (easier to debug)
    int num_goroutines = 3;
    printf("Creating %d goroutines...\n\n", num_goroutines);
    
    for (int i = 1; i <= num_goroutines; i++) {
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
    
    printf("\n╔══════════════════════════════════════════╗\n");
    printf("║  Test Completed Successfully!            ║\n");
    printf("╚══════════════════════════════════════════╝\n");
    return 0;
}
