#include "worker_stealing.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Test function that does CPU work
void cpu_intensive(void* arg) {
    int id = *(int*)arg;
    free(arg);
    
    printf("Goroutine %d starting CPU work\n", id);
    
    // Simulate CPU work
    volatile long sum = 0;
    for (long i = 0; i < 10000000; i++) {
        sum += i;
    }
    
    printf("Goroutine %d finished (sum=%ld)\n", id, sum);
}

// Test function that yields
void yielding_task(void* arg) {
    int id = *(int*)arg;
    free(arg);
    
    printf("Goroutine %d: starting\n", id);
    
    for (int i = 0; i < 3; i++) {
        printf("Goroutine %d: iteration %d\n", id, i);
        usleep(10000);  // Sleep 10ms (simulates I/O)
    }
    
    printf("Goroutine %d: finished\n", id);
}

// Test unbalanced load (stress work-stealing)
void unbalanced_load(void* arg) {
    int id = *(int*)arg;
    free(arg);
    
    // Some tasks are long, some short
    if (id % 4 == 0) {
        // Long task
        printf("Long task %d starting\n", id);
        usleep(100000);  // 100ms
    } else {
        // Short task
        usleep(10000);  // 10ms
    }
    
    printf("Task %d done\n", id);
}

int main() {
    printf("=== Work-Stealing Scheduler Test ===\n\n");
    
    scheduler_stealing_t s;
    
    // Initialize with 4 workers (2x dual-core CPU)
    if (scheduler_stealing_init(&s, 4) != 0) {
        fprintf(stderr, "Failed to initialize scheduler\n");
        return 1;
    }
    
    printf("Test 1: CPU-intensive tasks\n");
    printf("Creating 100 CPU-intensive goroutines...\n");
    for (int i = 0; i < 100; i++) {
        int* id = malloc(sizeof(int));
        *id = i;
        goroutine_t* g = create_goroutine(cpu_intensive, id);
        scheduler_stealing_add(&s, g);
    }
    
    scheduler_stealing_start(&s);
    scheduler_stealing_wait(&s);
    
    printf("\nTest 2: Yielding tasks\n");
    for (int i = 0; i < 50; i++) {
        int* id = malloc(sizeof(int));
        *id = i + 100;
        goroutine_t* g = create_goroutine(yielding_task, id);
        scheduler_stealing_add(&s, g);
    }
    
    scheduler_stealing_start(&s);
    scheduler_stealing_wait(&s);
    
    printf("\nTest 3: Unbalanced load (stress work-stealing)\n");
    for (int i = 0; i < 200; i++) {
        int* id = malloc(sizeof(int));
        *id = i + 200;
        goroutine_t* g = create_goroutine(unbalanced_load, id);
        scheduler_stealing_add(&s, g);
    }
    
    scheduler_stealing_start(&s);
    scheduler_stealing_wait(&s);
    
    scheduler_stealing_destroy(&s);
    
    printf("\n=== All tests passed! ===\n");
    return 0;
}

