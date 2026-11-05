#include "async_io.h"
#include "worker_stealing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/socket.h>
#include <sys/timerfd.h>

static io_reactor_t reactor;
static pthread_t reactor_thread;

typedef struct async_sleep_data {
    future_t* future;
    uint64_t milliseconds;
} async_sleep_data_t;

typedef struct async_select_request {
    async_select_case_t* cases;
    int ncases;
    atomic_int result;
    atomic_int error_code;
    int done;
    uint64_t timeout_ms;
    goroutine_t* waiting_goroutine;
    pthread_mutex_t lock;
    pthread_cond_t completed;
} async_select_request_t;

static void mark_future_complete(future_t* f, int result) {
    f->result = result;
    atomic_store_explicit(&f->completed, 1, memory_order_release);
}

static void* async_sleep_timer(void* arg) {
    async_sleep_data_t* data = arg;
    usleep(data->milliseconds * 1000);
    mark_future_complete(data->future, 0);
    if (data->future->waiting_goroutine) {
        scheduler_ready(data->future->waiting_goroutine);
    }
    free(data);
    return NULL;
}

// Set non-blocking flag on file descriptor
static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int async_io_init(void) {
    memset(&reactor, 0, sizeof(reactor));
    
    reactor.epoll_fd = epoll_create1(0);
    if (reactor.epoll_fd == -1) {
        perror("epoll_create1");
        return -1;
    }
    
    pthread_mutex_init(&reactor.lock, NULL);
    reactor.running = 1;
    
    // Create reactor thread
    if (pthread_create(&reactor_thread, NULL, io_reactor_thread, NULL) != 0) {
        perror("pthread_create");
        close(reactor.epoll_fd);
        return -1;
    }
    
    printf("Async I/O reactor initialized (epoll_fd=%d)\n", reactor.epoll_fd);
    return 0;
}

void async_io_shutdown(void) {
    reactor.running = 0;
    pthread_join(reactor_thread, NULL);
    close(reactor.epoll_fd);
    pthread_mutex_destroy(&reactor.lock);
    printf("Async I/O reactor shutdown\n");
}

// Helper to register fd with epoll
static int register_io(int fd, io_event_t* event, uint32_t events) {
    struct epoll_event ev;
    ev.events = events;
    ev.data.ptr = event;
    
    if (epoll_ctl(reactor.epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        // If already registered, modify
        if (errno == EEXIST) {
            if (epoll_ctl(reactor.epoll_fd, EPOLL_CTL_MOD, fd, &ev) == -1) {
                perror("epoll_ctl MOD");
                return -1;
            }
        } else {
            perror("epoll_ctl ADD");
            return -1;
        }
    }
    
    return 0;
}

future_t* async_read(int fd, void* buf, size_t count) {
    future_t* f = calloc(1, sizeof(future_t));
    if (!f) return NULL;
    atomic_init(&f->completed, 0);
    
    io_event_t* event = calloc(1, sizeof(io_event_t));
    if (!event) {
        free(f);
        return NULL;
    }
    
    event->fd = fd;
    event->op = IO_READ;
    event->future = f;
    event->buffer = buf;
    event->count = count;
    
    // Set non-blocking
    if (set_nonblocking(fd) == -1) {
        mark_future_complete(f, -1);
        free(event);
        return f;
    }
    
    // Try immediate read
    ssize_t n = read(fd, buf, count);
    if (n >= 0) {
        // Data available immediately
        mark_future_complete(f, (int)n);
        free(event);
        return f;
    }
    
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
        // Real error
        mark_future_complete(f, -1);
        free(event);
        return f;
    }
    
    // Would block - register with epoll
    f->waiting_goroutine = get_current_goroutine();  // Need to implement this
    event->future = f;
    
    pthread_mutex_lock(&reactor.lock);
    if (register_io(fd, event, EPOLLIN | EPOLLONESHOT) == -1) {
        pthread_mutex_unlock(&reactor.lock);
        mark_future_complete(f, -1);
        free(event);
        return f;
    }
    event->next = reactor.pending_reads;
    reactor.pending_reads = event;
    pthread_mutex_unlock(&reactor.lock);
    
    park_goroutine();
    
    return f;
}

future_t* async_write(int fd, const void* buf, size_t count) {
    future_t* f = calloc(1, sizeof(future_t));
    if (!f) return NULL;
    atomic_init(&f->completed, 0);
    
    io_event_t* event = calloc(1, sizeof(io_event_t));
    if (!event) {
        free(f);
        return NULL;
    }
    
    event->fd = fd;
    event->op = IO_WRITE;
    event->future = f;
    event->buffer = (void*)buf;
    event->count = count;
    
    set_nonblocking(fd);
    
    // Try immediate write
    ssize_t n = write(fd, buf, count);
    if (n >= 0) {
        mark_future_complete(f, (int)n);
        free(event);
        return f;
    }
    
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
        mark_future_complete(f, -1);
        free(event);
        return f;
    }
    
    // Would block - register
    f->waiting_goroutine = get_current_goroutine();
    event->future = f;
    
    pthread_mutex_lock(&reactor.lock);
    if (register_io(fd, event, EPOLLOUT | EPOLLONESHOT) == -1) {
        pthread_mutex_unlock(&reactor.lock);
        mark_future_complete(f, -1);
        free(event);
        return f;
    }
    event->next = reactor.pending_writes;
    reactor.pending_writes = event;
    pthread_mutex_unlock(&reactor.lock);
    
    park_goroutine();
    
    return f;
}

future_t* async_sleep(uint64_t milliseconds) {
    future_t* f = calloc(1, sizeof(future_t));
    if (!f) return NULL;
    atomic_init(&f->completed, 0);
    f->waiting_goroutine = get_current_goroutine();

    async_sleep_data_t* data = malloc(sizeof(*data));
    if (!data) {
        free(f);
        return NULL;
    }
    data->future = f;
    data->milliseconds = milliseconds;

    pthread_t timer_thread;
    int error = pthread_create(&timer_thread, NULL, async_sleep_timer, data);
    if (error != 0) {
        free(data);
        free(f);
        errno = error;
        perror("pthread_create");
        return NULL;
    }
    pthread_detach(timer_thread);
    
    park_goroutine();
    
    return f;
}

static void* async_select_thread(void* arg) {
    async_select_request_t* request = arg;
    int epoll_fd = -1;
    int timer_fd = -1;
    int* duplicated_fds = NULL;
    int result = -2;
    int select_errno = 0;

    duplicated_fds = malloc((size_t)request->ncases * sizeof(*duplicated_fds));
    if (!duplicated_fds) goto complete;
    for (int i = 0; i < request->ncases; i++) duplicated_fds[i] = -1;

    epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd == -1) goto complete;

    for (int i = 0; i < request->ncases; i++) {
        async_select_case_t* select_case = &request->cases[i];
        if (select_case->type == ASYNC_WAIT_TIMEOUT) continue;

        int duplicate = dup(select_case->fd);
        if (duplicate == -1) goto complete;
        duplicated_fds[i] = duplicate;

        struct epoll_event event = {0};
        event.events = (select_case->type == ASYNC_WAIT_READ ? EPOLLIN : EPOLLOUT) |
                       EPOLLONESHOT;
        event.data.u64 = (uint64_t)i;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, duplicate, &event) == -1) {
            goto complete;
        }
    }

    timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    if (timer_fd == -1) goto complete;

    struct epoll_event timer_event = {0};
    timer_event.events = EPOLLIN;
    timer_event.data.u64 = UINT64_MAX;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, timer_fd, &timer_event) == -1) {
        goto complete;
    }

    if (request->timeout_ms) {
        struct itimerspec timer = {0};
        timer.it_value.tv_sec = (time_t)(request->timeout_ms / 1000);
        timer.it_value.tv_nsec =
            (long)(request->timeout_ms % 1000) * 1000000L;
        if (timerfd_settime(timer_fd, 0, &timer, NULL) == -1) goto complete;
    } else {
        struct epoll_event events[16];
        int ready = epoll_wait(epoll_fd, events, 16, 0);
        if (ready == -1) goto complete;
        result = -1;
        for (int i = 0; i < ready; i++) {
            if (events[i].data.u64 != UINT64_MAX) {
                result = (int)events[i].data.u64;
                break;
            }
        }
        if (result == -1) {
            for (int i = 0; i < request->ncases; i++) {
                if (request->cases[i].type == ASYNC_WAIT_TIMEOUT) {
                    result = i;
                    break;
                }
            }
        }
        goto complete;
    }

    for (;;) {
        struct epoll_event events[16];
        int ready = epoll_wait(epoll_fd, events, 16, -1);
        if (ready == -1) {
            if (errno == EINTR) continue;
            goto complete;
        }

        for (int i = 0; i < ready; i++) {
            if (events[i].data.u64 == UINT64_MAX) continue;
            result = (int)events[i].data.u64;
            goto complete;
        }

        uint64_t expirations;
        if (read(timer_fd, &expirations, sizeof(expirations)) == sizeof(expirations)) {
            result = -1;
            for (int i = 0; i < request->ncases; i++) {
                if (request->cases[i].type == ASYNC_WAIT_TIMEOUT) {
                    result = i;
                    break;
                }
            }
            goto complete;
        }
    }

complete:
    if (result == -2) select_errno = errno;
    if (epoll_fd != -1) close(epoll_fd);
    if (timer_fd != -1) close(timer_fd);
    if (duplicated_fds) {
        for (int i = 0; i < request->ncases; i++) {
            if (duplicated_fds[i] != -1) close(duplicated_fds[i]);
        }
        free(duplicated_fds);
    }

    if (result >= 0 && result < request->ncases) {
        future_t* future = request->cases[result].future;
        if (future) mark_future_complete(future, 1);
    }

    pthread_mutex_lock(&request->lock);
    atomic_store_explicit(&request->result, result, memory_order_release);
    atomic_store_explicit(&request->error_code, select_errno, memory_order_release);
    request->done = 1;
    pthread_cond_broadcast(&request->completed);
    pthread_mutex_unlock(&request->lock);

    if (request->waiting_goroutine) {
        scheduler_ready(request->waiting_goroutine);
    }
    return NULL;
}

int async_select(async_select_case_t* cases, int ncases, uint64_t timeout_ms) {
    if (!cases || ncases <= 0 ||
        (size_t)ncases > SIZE_MAX / sizeof(*cases)) {
        errno = EINVAL;
        return -2;
    }

    for (int i = 0; i < ncases; i++) {
        if (cases[i].type != ASYNC_WAIT_READ &&
            cases[i].type != ASYNC_WAIT_WRITE &&
            cases[i].type != ASYNC_WAIT_TIMEOUT) {
            errno = EINVAL;
            return -2;
        }
    }

    async_select_request_t* request = calloc(1, sizeof(*request));
    if (!request) return -2;
    request->cases = malloc((size_t)ncases * sizeof(*cases));
    if (!request->cases) {
        free(request);
        return -2;
    }
    memcpy(request->cases, cases, (size_t)ncases * sizeof(*cases));
    request->ncases = ncases;
    request->timeout_ms = timeout_ms;
    request->waiting_goroutine = get_current_goroutine();
    atomic_init(&request->result, -2);
    atomic_init(&request->error_code, 0);

    int error = pthread_mutex_init(&request->lock, NULL);
    if (error != 0) {
        free(request->cases);
        free(request);
        errno = error;
        return -2;
    }
    error = pthread_cond_init(&request->completed, NULL);
    if (error != 0) {
        pthread_mutex_destroy(&request->lock);
        free(request->cases);
        free(request);
        errno = error;
        return -2;
    }

    pthread_t thread;
    error = pthread_create(&thread, NULL, async_select_thread, request);
    if (error != 0) {
        pthread_cond_destroy(&request->completed);
        pthread_mutex_destroy(&request->lock);
        free(request->cases);
        free(request);
        errno = error;
        return -2;
    }
    pthread_detach(thread);

    if (request->waiting_goroutine) {
        park_goroutine();
    } else {
        pthread_mutex_lock(&request->lock);
        while (!request->done) {
            pthread_cond_wait(&request->completed, &request->lock);
        }
        pthread_mutex_unlock(&request->lock);
    }

    int result = atomic_load_explicit(&request->result, memory_order_acquire);
    int select_errno = atomic_load_explicit(&request->error_code,
                                            memory_order_acquire);
    pthread_cond_destroy(&request->completed);
    pthread_mutex_destroy(&request->lock);
    free(request->cases);
    free(request);
    if (result == -2 && select_errno) errno = select_errno;
    return result;
}

void* io_reactor_thread(void* arg) {
    (void)arg;
    printf("I/O Reactor thread started\n");
    
    while (reactor.running) {
        int nfds = epoll_wait(reactor.epoll_fd, reactor.events, 1024, 100);
        
        for (int i = 0; i < nfds; i++) {
            io_event_t* event = (io_event_t*)reactor.events[i].data.ptr;
            future_t* f = event->future;
            
            // Perform the actual I/O
            if (event->op == IO_READ) {
                ssize_t n = read(event->fd, event->buffer, event->count);
                mark_future_complete(f, (int)n);
            } else if (event->op == IO_WRITE) {
                ssize_t n = write(event->fd, event->buffer, event->count);
                mark_future_complete(f, (int)n);
            } else {
                mark_future_complete(f, -1);
            }
            
            // Wake up the waiting goroutine
            if (f->waiting_goroutine) {
                scheduler_ready(f->waiting_goroutine);
            }
            
            // Remove from epoll
            epoll_ctl(reactor.epoll_fd, EPOLL_CTL_DEL, event->fd, NULL);
            free(event);
        }
    }
    
    return NULL;
}

int future_wait(future_t* f) {
    while (!atomic_load_explicit(&f->completed, memory_order_acquire)) {
        usleep(1000);
    }
    return f->result;
}

int future_try_wait(future_t* f) {
    return atomic_load_explicit(&f->completed, memory_order_acquire);
}

void future_free(future_t* f) {
    free(f);
}
