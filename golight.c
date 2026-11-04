#include "golight.h"
#include "worker_stealing.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>

typedef struct runtime_channel_entry {
    channel_t* channel;
    struct runtime_channel_entry* next;
} runtime_channel_entry_t;

typedef struct waitgroup_waiter {
    goroutine_t* goroutine;
    channel_t* thread_channel;
    struct waitgroup_waiter* next;
} waitgroup_waiter_t;

struct waitgroup {
    atomic_int counter;
    pthread_mutex_t lock;
    waitgroup_waiter_t* waiters;
};

static scheduler_stealing_t runtime_scheduler;
static int runtime_initialized;
static int runtime_waited;
static pthread_mutex_t channels_lock = PTHREAD_MUTEX_INITIALIZER;
static runtime_channel_entry_t* runtime_channels;

static void unregister_channel(channel_t* channel) {
    pthread_mutex_lock(&channels_lock);
    runtime_channel_entry_t** entry = &runtime_channels;
    while (*entry && (*entry)->channel != channel) {
        entry = &(*entry)->next;
    }
    if (*entry) {
        runtime_channel_entry_t* removed = *entry;
        *entry = removed->next;
        free(removed);
    }
    pthread_mutex_unlock(&channels_lock);
}

static void destroy_runtime_channels(void) {
    pthread_mutex_lock(&channels_lock);
    runtime_channel_entry_t* entry = runtime_channels;
    runtime_channels = NULL;
    pthread_mutex_unlock(&channels_lock);

    while (entry) {
        runtime_channel_entry_t* next = entry->next;
        channel_destroy(entry->channel);
        free(entry);
        entry = next;
    }
}

int runtime_init(void) {
    if (runtime_initialized) {
        errno = EALREADY;
        return -1;
    }

    if (scheduler_stealing_init(&runtime_scheduler, 0) != 0) {
        errno = ENOMEM;
        return -1;
    }
    if (async_io_init() != 0) {
        scheduler_stealing_destroy(&runtime_scheduler);
        return -1;
    }

    runtime_initialized = 1;
    runtime_waited = 0;
    scheduler_stealing_start(&runtime_scheduler);
    return 0;
}

int go(void (*function)(void*), void* arg) {
    if (!runtime_initialized || runtime_waited || !function) {
        errno = EINVAL;
        return -1;
    }

    goroutine_t* g = create_goroutine(function, arg);
    if (!g) {
        errno = ENOMEM;
        return -1;
    }
    scheduler_stealing_add(&runtime_scheduler, g);
    return 0;
}

channel_t* make_channel(int capacity) {
    if (capacity <= 0) {
        errno = EINVAL;
        return NULL;
    }
    channel_t* channel = channel_create(capacity);
    if (!channel) return NULL;

    runtime_channel_entry_t* entry = malloc(sizeof(*entry));
    if (!entry) {
        channel_destroy(channel);
        errno = ENOMEM;
        return NULL;
    }
    entry->channel = channel;
    pthread_mutex_lock(&channels_lock);
    entry->next = runtime_channels;
    runtime_channels = entry;
    pthread_mutex_unlock(&channels_lock);
    return channel;
}

void runtime_wait(void) {
    if (runtime_initialized && !runtime_waited) {
        scheduler_stealing_wait(&runtime_scheduler);
        runtime_waited = 1;
    }
}

void runtime_shutdown(void) {
    if (runtime_initialized) {
        runtime_wait();
        async_io_shutdown();
        scheduler_stealing_destroy(&runtime_scheduler);
    }
    destroy_runtime_channels();
    runtime_initialized = 0;
    runtime_waited = 0;
}

void yield(void) {
    if (scheduler_yield_current() != 0) {
        errno = EINVAL;
    }
}

uint64_t goroutine_id(void) {
    goroutine_t* current = get_current_goroutine();
    return current ? (uint64_t)current->id : 0;
}

void chan_send(channel_t* ch, void* data) {
    if (!ch) {
        errno = EINVAL;
        return;
    }
    channel_send(ch, data);
}

void* chan_receive(channel_t* ch) {
    if (!ch) {
        errno = EINVAL;
        return NULL;
    }
    return channel_receive(ch);
}

void chan_close(channel_t* ch) {
    if (!ch) return;
    unregister_channel(ch);
    channel_destroy(ch);
}

waitgroup_t waitgroup_create(void) {
    waitgroup_t wg = calloc(1, sizeof(*wg));
    if (!wg) return NULL;

    if (pthread_mutex_init(&wg->lock, NULL) != 0) {
        free(wg);
        errno = ENOMEM;
        return NULL;
    }
    atomic_init(&wg->counter, 0);
    return wg;
}

void waitgroup_add(waitgroup_t wg, int delta) {
    if (!wg) {
        errno = EINVAL;
        return;
    }
    if (delta == 0) return;

    pthread_mutex_lock(&wg->lock);
    int current = atomic_load(&wg->counter);
    long long updated = (long long)current + delta;
    if (updated < 0 || updated > INT_MAX) {
        pthread_mutex_unlock(&wg->lock);
        errno = EINVAL;
        return;
    }

    atomic_store(&wg->counter, (int)updated);

    waitgroup_waiter_t* ready_waiters = NULL;
    if (updated == 0) {
        ready_waiters = wg->waiters;
        wg->waiters = NULL;
    }
    pthread_mutex_unlock(&wg->lock);

    while (ready_waiters) {
        waitgroup_waiter_t* next = ready_waiters->next;
        if (ready_waiters->goroutine) {
            scheduler_ready(ready_waiters->goroutine);
        } else {
            channel_send(ready_waiters->thread_channel, wg);
        }
        ready_waiters = next;
    }
}

void waitgroup_done(waitgroup_t wg) {
    waitgroup_add(wg, -1);
}

void waitgroup_wait(waitgroup_t wg) {
    if (!wg) {
        errno = EINVAL;
        return;
    }

    goroutine_t* current = get_current_goroutine();
    waitgroup_waiter_t waiter = {
        .goroutine = current,
        .thread_channel = NULL,
        .next = NULL,
    };
    if (!current) {
        waiter.thread_channel = channel_create(1);
        if (!waiter.thread_channel) return;
    }

    pthread_mutex_lock(&wg->lock);
    if (atomic_load(&wg->counter) == 0) {
        pthread_mutex_unlock(&wg->lock);
        if (waiter.thread_channel) channel_destroy(waiter.thread_channel);
        return;
    }

    waiter.next = wg->waiters;
    wg->waiters = &waiter;
    pthread_mutex_unlock(&wg->lock);

    if (current) {
        park_goroutine();
    } else {
        (void)channel_receive(waiter.thread_channel);
        channel_destroy(waiter.thread_channel);
    }
}

void waitgroup_destroy(waitgroup_t wg) {
    if (!wg) return;
    pthread_mutex_lock(&wg->lock);
    if (atomic_load(&wg->counter) != 0 || wg->waiters) {
        pthread_mutex_unlock(&wg->lock);
        errno = EBUSY;
        return;
    }
    pthread_mutex_unlock(&wg->lock);
    pthread_mutex_destroy(&wg->lock);
    free(wg);
}
