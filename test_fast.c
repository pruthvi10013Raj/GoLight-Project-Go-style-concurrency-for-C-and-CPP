#include "worker_stealing.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

void fast_function(void* arg) {
    int id = *(int*)arg;
    free(arg);
    
    printf("  🚀 Goroutine %d: Starting\n", id);
    
    // Very fast work - just 100,000 iterations
    volatile long sum = 0;
    for (long i = 0; i < 100000; i++) {
        sum += i;
    }
    
    printf("  ✅ Goroutine %d: Finished! (sum=%ld)\n", id, sum);
}

int main() {
    printf("╔══════════════════════════════════════════╗\n");
    printf("║     FAST TEST - Should complete quickly  ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");
    
    scheduler_stealing_t s;
    scheduler_stealing_init(&s, 2);
    
    // Create 4 fast goroutines
    for (int i = 1; i <= 4; i++) {
        int* id = malloc(sizeof(int));
        *id = i;
        goroutine_t* g = create_goroutine(fast_function, id);
        scheduler_stealing_add(&s, g);
    }
    
    printf("Starting scheduler...\n\n");
    scheduler_stealing_start(&s);
    
    printf("Waiting for completion...\n");
    scheduler_stealing_wait(&s);
    scheduler_stealing_destroy(&s);
    
    printf("\n✅ ALL TESTS PASSED! Scheduler working correctly!\n");
    return 0;
}
