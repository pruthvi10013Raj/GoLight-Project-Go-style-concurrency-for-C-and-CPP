# GoLight

Go-style cooperative concurrency for C and C++ on Linux.

**Status: Core complete (P1–P5).** The current implementation includes a
work-stealing scheduler, goroutine-aware channel and async-I/O parking, a unified
API, guarded stacks, and fd-based `async_select()`.

## Features

| Feature | Status |
|---------|--------|
| Goroutine creation and `ucontext` switching | Working; tested |
| Work-stealing scheduler | Implemented; test result noted under [Tests](#tests) |
| `park_goroutine()` / `scheduler_ready()` | Implemented with context switching and deque scheduling |
| `async_read()` / `async_write()` scheduler integration | Implemented |
| Unified runtime, channel, and WaitGroup API | Implemented |
| Channels that park goroutines instead of workers | Implemented; channel tests available |
| Stack guard pages and overflow diagnostic | Implemented; overflow test exits with status 1 by design |
| fd readiness and timeout `async_select()` | Implemented and tested |
| Goroutine-based echo server | Source present; see test status |
| Cross-platform support and profiling | Out of scope for this version |

## API

Include `golight.h` to use the unified API:

```c
#include "golight.h"

int runtime_init(void);
int go(void (*function)(void *), void *arg);
channel_t *make_channel(int capacity);
void chan_send(channel_t *channel, void *data);
void *chan_receive(channel_t *channel);
void chan_close(channel_t *channel);
void runtime_wait(void);
void runtime_shutdown(void);
void yield(void);
uint64_t goroutine_id(void);

waitgroup_t waitgroup_create(void);
void waitgroup_add(waitgroup_t group, int delta);
void waitgroup_done(waitgroup_t group);
void waitgroup_wait(waitgroup_t group);
void waitgroup_destroy(waitgroup_t group);

future_t *async_read(int fd, void *buffer, size_t count);
future_t *async_write(int fd, const void *buffer, size_t count);
future_t *async_sleep(uint64_t milliseconds);
int async_select(async_select_case_t *cases, int ncases, uint64_t timeout_ms);
```

`async_select()` multiplexes file descriptors and a timeout. It does not select
on channels. The runtime is cooperative: CPU-bound goroutines must yield or
park to let other goroutines on their worker run.

## Quick Start

Save this as `demo.c`:

```c
#include "golight.h"

#include <stdio.h>
#include <stdlib.h>

static waitgroup_t tasks;

static void work(void *arg) {
    (void)arg;
    puts("Hello from a GoLight goroutine");
    waitgroup_done(tasks);
}

int main(void) {
    if (runtime_init() != 0) {
        perror("runtime_init");
        return EXIT_FAILURE;
    }

    tasks = waitgroup_create();
    if (!tasks) {
        perror("waitgroup_create");
        runtime_shutdown();
        return EXIT_FAILURE;
    }

    waitgroup_add(tasks, 1);
    if (go(work, NULL) != 0) {
        perror("go");
        waitgroup_done(tasks);
        waitgroup_wait(tasks);
        waitgroup_destroy(tasks);
        runtime_shutdown();
        return EXIT_FAILURE;
    }

    waitgroup_wait(tasks);
    runtime_wait();
    waitgroup_destroy(tasks);
    runtime_shutdown();
    return EXIT_SUCCESS;
}
```

Build and run:

```bash
make golight
gcc -pthread -o demo demo.c -L. -lgolight
./demo
```

## Build

```bash
make golight
```

This builds the static library `libgolight.a`. The Makefile uses GCC with
`-Wall -O2 -pthread -D_GNU_SOURCE`.

## Tests

Build targets provided by the Makefile include `test_100_fast`,
`test_channel_integration`, `test_channel_no_blocking`,
`test_stack_overflow`, `test_full_integration`, and `test_async_select`.
`test_work_stealing` and `test_async_goroutine` sources are also present.

| Program | Verification notes |
|---------|--------------------|
| `test_100_fast` | Last run completed 100 goroutines successfully |
| `test_work_stealing` | Present; one run in the current documentation audit exited 139. Do not treat as reliably passing without rerunning |
| `test_channel_integration` | Last run completed 100 sender and 100 receiver tasks |
| `test_channel_no_blocking` | Last run passed; all ten ticks appeared while the receive was parked |
| `test_stack_overflow` | Expected result is a guard-page diagnostic and exit status 1 |
| `test_full_integration` | Present; one run in the current documentation audit exited 139. Do not treat as reliably passing without rerunning |
| `test_async_select` | fd readiness, timeout, and worker-responsiveness checks passed |
| `test_async_goroutine` | Last run completed 50/50 reads |
| `echo_server_goroutines` | 100 local concurrent requests passed; server reported 100/100 clients |

Build and run the Makefile test targets individually, for example:

```bash
make test_100_fast test_channel_integration test_channel_no_blocking \
    test_stack_overflow test_full_integration test_async_select
./test_100_fast
./test_channel_integration
./test_channel_no_blocking
./test_stack_overflow
./test_full_integration
./test_async_select
```

The stack-overflow test intentionally terminates the process with exit status
1 after printing its diagnostic.

### Benchmark

`make bench` runs `benchmark.c`, which schedules 10,000 no-op tasks at 1, 2, 4,
and 8 workers. One run in this environment reported:

| Workers | Elapsed time | Throughput |
|---------|--------------|------------|
| 1 | 0.658 s | 15,208 tasks/s |
| 2 | 0.611 s | 16,362 tasks/s |
| 4 | 0.629 s | 15,900 tasks/s |
| 8 | 0.622 s | 16,069 tasks/s |

These are a single run, not stable performance guarantees. They do not support
the previously documented 7.2× speedup, 47 ns context-switch, 85 ns creation,
or 95% CPU-utilization claims.

## Scope & Limitations

Linux-only (epoll). Cooperative scheduling. Cross-platform support and profiling
are out of scope for this version.
