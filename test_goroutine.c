#include "goroutine.h"
#include <stdio.h>

void my_function(void *arg) {
        int id = *(int*)arg;
        printf("Goroutine %d is running!\n", id);
        printf("Goroutine %d is doing work...\n", id);
}

int main(){
        printf("=== Testing simple Goroutine ===\n");

        int id1 = 1, id2 = 2;

        //now here we create goroutines
        goroutine_t *g1 = create_goroutine(my_function, &id1);
        goroutine_t *g2 = create_goroutine(my_function, &id2);

        //for the sake of now, these will run sequentially
        //proper schedular will add afterward
        run_goroutine(g1);

        //since we don't have schedular yet, we cannot run the g2, for the sake of now

        printf("Main function ending\n");

        free_goroutine(g1);
        free_goroutine(g2);

        return 0;

}






