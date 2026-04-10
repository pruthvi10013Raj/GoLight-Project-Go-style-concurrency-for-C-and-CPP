#include "worker_stealing.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define BENCHMARK_TASKS 10000

void empty_task(void* arg) {
    // Do nothing
}

double benchmark_scheduler(scheduler_stealing_t* s, int task_count) {
    struct timespec start, end;
    
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    for (int i = 0; i < task_count; i++) {
        int* id = malloc(sizeof(int));
        *id = i;
        goroutine_t* g = create_goroutine(empty_task, id);
        scheduler_stealing_add(s, g);
    }
    
    scheduler_stealing_start(s);
    scheduler_stealing_wait(s);
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    double elapsed = (end.tv_sec - start.tv_sec) + 
                     (end.tv_nsec - start.tv_nsec) / 1000000000.0;
    
    return elapsed;
}

int main() {
    printf("=== Work-Stealing Performance Benchmark ===\n\n");
    
    for (int workers = 1; workers <= 8; workers *= 2) {
        scheduler_stealing_t s;
        scheduler_stealing_init(&s, workers);
        
        double time = benchmark_scheduler(&s, BENCHMARK_TASKS);
        double throughput = BENCHMARK_TASKS / time;
        
        printf("Workers: %2d | Time: %.3fs | Throughput: %.0f tasks/sec\n",
               workers, time, throughput);
        
        scheduler_stealing_destroy(&s);
    }
    
    printf("\nBenchmark complete!\n");
    return 0;
}
