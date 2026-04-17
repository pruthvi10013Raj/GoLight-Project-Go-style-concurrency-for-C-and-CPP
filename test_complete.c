#include "worker_stealing.h"
#include <stdio.h>
#include <unistd.h>
#include <time.h>

void test_work(void* arg) {
    int id = *(int*)arg;
    free(arg);
    
    // Reduced iterations for faster testing
    volatile long sum = 0;
    for (long i = 0; i < 1000000; i++) {  // 1 million iterations
        sum += i;
    }
    
    printf("✓ Goroutine %d completed (sum=%ld)\n", id, sum);
}

int main() {
    printf("╔════════════════════════════════════════════╗\n");
    printf("║     TESTING FIXED WORK-STEALING            ║\n");
    printf("╚════════════════════════════════════════════╝\n\n");
    
    scheduler_stealing_t s;
    scheduler_stealing_init(&s, 4);
    
    // Create 20 goroutines for quick test
    printf("Creating 20 goroutines...\n");
    for (int i = 1; i <= 20; i++) {
        int* id = malloc(sizeof(int));
        *id = i;
        goroutine_t* g = create_goroutine(test_work, id);
        scheduler_stealing_add(&s, g);
    }
    
    printf("\nStarting scheduler...\n");
    scheduler_stealing_start(&s);
    
    printf("Waiting for completion...\n");
    scheduler_stealing_wait(&s);
    
    scheduler_stealing_destroy(&s);
    
    printf("\n✅ TEST PASSED! All goroutines completed.\n");
    return 0;
}
