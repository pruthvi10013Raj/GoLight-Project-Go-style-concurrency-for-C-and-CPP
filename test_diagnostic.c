#include "worker_stealing.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

volatile int running = 1;

void alarm_handler(int sig) {
    printf("\n!!! TIMEOUT - Program still running after 10 seconds !!!\n");
    printf("Printing debug info...\n");
    running = 0;
}

void test_function(void* arg) {
    int id = *(int*)arg;
    free(arg);
    
    printf("  Goroutine %d: Working...\n", id);
    
    // Do some work
    volatile long sum = 0;
    for (long i = 0; i < 1000000; i++) {
        sum += i;
    }
    
    printf("  Goroutine %d: Done! (sum=%ld)\n", id, sum);
}

int main() {
    // Set alarm to prevent infinite hang
    signal(SIGALRM, alarm_handler);
    alarm(10);  // 10 second timeout
    
    printf("=== Diagnostic Test ===\n\n");
    
    scheduler_stealing_t s;
    scheduler_stealing_init(&s, 2);
    
    // Create just 2 goroutines
    for (int i = 1; i <= 2; i++) {
        int* id = malloc(sizeof(int));
        *id = i;
        goroutine_t* g = create_goroutine(test_function, id);
        scheduler_stealing_add(&s, g);
    }
    
    printf("Starting scheduler...\n");
    scheduler_stealing_start(&s);
    
    printf("Waiting (will timeout after 10 seconds)...\n");
    
    // Manual wait with timeout
    int wait_count = 0;
    while (atomic_load(&s.active_goroutines) > 0 && wait_count < 20 && running) {
        printf("  Active goroutines: %d\n", atomic_load(&s.active_goroutines));
        usleep(500000);
        wait_count++;
    }
    
    if (atomic_load(&s.active_goroutines) > 0) {
        printf("ERROR: Still have %d active goroutines!\n", 
               atomic_load(&s.active_goroutines));
    } else {
        printf("SUCCESS: All goroutines completed!\n");
    }
    
    atomic_store(&s.running, 0);
    
    // Wait for workers
    for (int i = 0; i < s.worker_count; i++) {
        pthread_cancel(s.workers[i].thread);
        pthread_join(s.workers[i].thread, NULL);
    }
    
    scheduler_stealing_destroy(&s);
    
    return 0;
}
