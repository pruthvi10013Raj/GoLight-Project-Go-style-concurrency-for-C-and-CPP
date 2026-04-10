#include "goroutine.h"
#include "scheduler.h"
#include <stdio.h>
#include <unistd.h>  // For sleep
#include <stdlib.h>

void worker1(void *arg) {
    int id = *(int*)arg;
    printf("Worker %d: Starting task A\n", id);
    printf("Worker %d: Doing work...\n", id);
    printf("Worker %d: Task A complete\n", id);
}

void worker2(void *arg) {
    int id = *(int*)arg;
    printf("Worker %d: Starting task B\n", id);
    printf("Worker %d: Doing more work...\n", id);
    printf("Worker %d: Task B complete\n", id);
}

int main() {
    printf("=== Testing Round-Robin Scheduler ===\n");
    
    scheduler_t scheduler;
    scheduler_init(&scheduler);
    
    int ids[] = {1, 2, 3, 4};
    
    // Create and add goroutines
    for (int i = 0; i < 4; i++) {
        goroutine_t *g = create_goroutine(i % 2 == 0 ? worker1 : worker2, &ids[i]);
        scheduler_add(&scheduler, g);
    }
    
    // Run all goroutines
    scheduler_run(&scheduler);
    
    printf("\n=== Test Complete ===\n");
    
    // Cleanup (in real implementation, scheduler would handle this)
    for (int i = 0; i < scheduler.count; i++) {
        free(scheduler.goroutines[i]->stack);
        free(scheduler.goroutines[i]);
    }
    
    return 0;
}
