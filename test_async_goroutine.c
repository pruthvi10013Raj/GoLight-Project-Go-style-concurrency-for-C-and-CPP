#include "golight.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define GOROUTINE_COUNT 50

static char fifo_paths[GOROUTINE_COUNT][256];
static int goroutine_ids[GOROUTINE_COUNT];
static atomic_int started_count;
static atomic_int completed_count;
static atomic_int failure_count;
static atomic_int writer_ready;

static void* release_fifo_reads(void* arg) {
    (void)arg;
    int write_fds[GOROUTINE_COUNT];

    for (int i = 0; i < GOROUTINE_COUNT; i++) {
        write_fds[i] = open(fifo_paths[i], O_RDWR | O_NONBLOCK);
        if (write_fds[i] == -1) {
            perror("open FIFO writer");
            atomic_fetch_add(&failure_count, 1);
            return NULL;
        }
    }
    atomic_store(&writer_ready, 1);

    while (atomic_load(&started_count) < GOROUTINE_COUNT) {
        usleep(1000);
    }

    usleep(250000);
    int completed_while_withheld = atomic_load(&completed_count);
    printf("Progress check: all %d read tasks started; %d completed while FIFO data was withheld\n",
           atomic_load(&started_count), completed_while_withheld);
    if (completed_while_withheld != 0) {
        atomic_fetch_add(&failure_count, 1);
    }

    for (int i = 0; i < GOROUTINE_COUNT; i++) {
        char byte = (char)i;
        if (write(write_fds[i], &byte, 1) != 1) {
            perror("write FIFO");
            atomic_fetch_add(&failure_count, 1);
        }
        close(write_fds[i]);
    }
    return NULL;
}

static void read_from_fifo(void* arg) {
    int id = *(int*)arg;
    int fd = open(fifo_paths[id], O_RDONLY | O_NONBLOCK);
    if (fd == -1) {
        perror("open FIFO reader");
        atomic_fetch_add(&failure_count, 1);
        return;
    }

    char buffer;
    atomic_fetch_add(&started_count, 1);
    future_t* future = async_read(fd, &buffer, sizeof(buffer));
    if (!future) {
        fprintf(stderr, "Goroutine %d: async_read failed to allocate a future\n", id);
        atomic_fetch_add(&failure_count, 1);
        close(fd);
        return;
    }

    int bytes_read = future_wait(future);
    future_free(future);
    close(fd);

    if (bytes_read != 1 || buffer != (char)id) {
        fprintf(stderr, "Goroutine %d: read failed (result=%d)\n", id, bytes_read);
        atomic_fetch_add(&failure_count, 1);
        return;
    }

    printf("Goroutine %d: read %d bytes\n", id, bytes_read);
    atomic_fetch_add(&completed_count, 1);
}

int main(void) {
    char temp_dir[] = "/tmp/golight-async-XXXXXX";
    if (!mkdtemp(temp_dir)) {
        perror("mkdtemp");
        return EXIT_FAILURE;
    }

    for (int i = 0; i < GOROUTINE_COUNT; i++) {
        snprintf(fifo_paths[i], sizeof(fifo_paths[i]), "%s/read-%d", temp_dir, i);
        if (mkfifo(fifo_paths[i], 0600) == -1) {
            perror("mkfifo");
            return EXIT_FAILURE;
        }
        goroutine_ids[i] = i;
    }

    if (runtime_init() != 0) {
        perror("runtime_init");
        return EXIT_FAILURE;
    }

    pthread_t writer_thread;
    if (pthread_create(&writer_thread, NULL, release_fifo_reads, NULL) != 0) {
        fprintf(stderr, "Failed to create FIFO writer thread\n");
        runtime_shutdown();
        return EXIT_FAILURE;
    }
    while (!atomic_load(&writer_ready)) {
        usleep(1000);
    }

    for (int i = 0; i < GOROUTINE_COUNT; i++) {
        if (go(read_from_fifo, &goroutine_ids[i]) != 0) {
            perror("go");
            return EXIT_FAILURE;
        }
    }

    runtime_wait();
    pthread_join(writer_thread, NULL);

    int completed = atomic_load(&completed_count);
    printf("Total goroutines completed: %d/%d\n", completed, GOROUTINE_COUNT);

    runtime_shutdown();
    for (int i = 0; i < GOROUTINE_COUNT; i++) {
        unlink(fifo_paths[i]);
    }
    rmdir(temp_dir);

    return completed == GOROUTINE_COUNT && atomic_load(&failure_count) == 0
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
