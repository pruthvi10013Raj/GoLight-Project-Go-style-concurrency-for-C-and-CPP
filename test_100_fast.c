#include "worker_stealing.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void fast_work(void* arg) {
    int id = *(int*)arg;
    free(arg);
    
    // Fast work - 100,000 iterations (0.001 seconds each)
    volatile long sum = 0;
    for (long i = 0; i < 100000; i++) {
        sum += i;
    }
    
    printf("Goroutine %d done\n", id);
}

int main() {
    printf("=== 100 Goroutines with FAST work ===\n\n");
    
    scheduler_stealing_t s;
    scheduler_stealing_init(&s, 4);
    
    printf("Creating 100 goroutines...\n");
    for (int i = 1; i <= 100; i++) {
        int* id = malloc(sizeof(int));
        *id = i;
        goroutine_t* g = create_goroutine(fast_work, id);
        scheduler_stealing_add(&s, g);
    }
    
    printf("\nStarting scheduler...\n");
    scheduler_stealing_start(&s);
    
    printf("Waiting (should complete in <2 seconds)...\n");
    scheduler_stealing_wait(&s);
    
    scheduler_stealing_destroy(&s);
    
    printf("\n✅ ALL 100 GOROUTINES COMPLETED!\n");
    return 0;
}
