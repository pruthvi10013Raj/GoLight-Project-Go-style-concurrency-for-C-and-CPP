# GoLight: Project Documentation and Handoff

**Status:** Core implementation P1–P5 complete
**Last updated:** 2026-11-05

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [Problem Statement](#2-problem-statement)
3. [Architecture](#3-architecture)
4. [File Structure & Status](#4-file-structure--status)
5. [Implementation Phases](#5-implementation-phases)
6. [API Inventory](#6-api-inventory)
7. [Key Algorithms](#7-key-algorithms)
8. [Build & Test Commands](#8-build--test-commands)
9. [Known Issues & Gotchas](#9-known-issues--gotchas)
10. [What's Left](#10-whats-left)
11. [Design Decisions & Rationale](#11-design-decisions--rationale)
12. [Literature / References](#12-literature--references)
13. [How to Pick This Up](#13-how-to-pick-this-up-for-another-ai)
14. [Honest Project Status Summary](#14-honest-project-status-summary)

## 1. Project Overview

GoLight is a Linux C runtime for cooperative user-level goroutines. The main
implementation combines `ucontext` switching, a pthread worker scheduler,
goroutine-aware channels, asynchronous fd I/O, guard-page stacks, and a unified
API in `golight.h`.

The benchmark currently measures 10,000 no-op tasks over 1, 2, 4, and 8 workers.
One run reported 15,208, 16,362, 15,900, and 16,069 tasks/second respectively.
This run does not support earlier claims of 7.2× speedup, 47 ns context
switching, 85 ns creation, or 95% CPU utilization. Do not treat a single
throughput run as a performance guarantee.

### Technology Stack

| Component | Implementation |
|-----------|----------------|
| Language | C |
| Worker threads | pthreads |
| Goroutine contexts | `ucontext` |
| Scheduler queues | Deques with mutex protection around concurrent access |
| Async fd readiness | Linux epoll |
| Select timeout | Linux `timerfd` |
| Goroutine stacks | `mmap` with a `PROT_NONE` guard page |
| Channel/waitgroup synchronization | C11 atomics and pthread mutexes |

## 2. Problem Statement

The project explores Go-style cooperative concurrency for C applications. Its
current scope is a Linux runtime API for spawning goroutines, yielding or
parking them, synchronizing via channels and waitgroups, and waiting for
asynchronous file-descriptor readiness.

This is a prototype/runtime project, not a claim of production-grade
performance, preemptive scheduling, cross-platform support, or dynamically
growing stacks.

## 3. Architecture

### 3.1 Layered Model

```text
Application: golight.h
  go / yield / waitgroup / channel aliases / async_read / async_select
Runtime: golight.c
  scheduler lifecycle and public wrappers
Scheduler: worker_stealing.c + deque.c
  pthread workers, ucontext dispatch, park and ready transitions
I/O: async_io.c
  epoll operations and per-call async_select waiter
Platform: pthreads, ucontext, epoll, timerfd, mmap
```

### 3.2 M:N Scheduling Model

Goroutines are user-level contexts dispatched by pthread workers. The scheduler
uses local and global deques. Queues are protected with mutexes around
concurrent accesses. A goroutine that has started is pinned to its original
worker when resumed because this implementation's `ucontext` continuation is
not migrated between pthreads.

### 3.3 Component Interaction

- `go()` creates a goroutine and queues it with the runtime scheduler.
- `yield()` saves the goroutine context and returns control to its worker.
- `park_goroutine()` saves the context and leaves it blocked until a
  `scheduler_ready()` call schedules it again.
- Channel wait queues and async I/O completion use that park/ready path.
- `async_select()` registers fd cases with epoll and a timerfd, then parks a
  calling goroutine until a case or timeout completes.

## 4. File Structure & Status

| File | Status | Purpose |
|------|--------|---------|
| `golight.h` | ✅ Working | Public unified API |
| `golight.c` | ✅ Working | Runtime lifecycle, wrappers, and WaitGroup |
| `goroutine.h` | ✅ Working | Goroutine, context, stack, and waiter metadata |
| `goroutine.c` | ✅ Working | Context setup, mmap stack, guard page, overflow handler |
| `worker_stealing.h` | ✅ Working | Scheduler and worker declarations |
| `worker_stealing.c` | ✅ Working | Worker dispatch, park/yield, ready scheduling |
| `deque.h`, `deque.c` | ✅ Working | Scheduler queues; concurrent use is mutex-protected |
| `channel.h`, `channel.c` | ✅ Working | Buffered/unbuffered channels with parked waiters |
| `async_io.h` | ✅ Working | Async I/O and fd select declarations |
| `async_io.c` | ✅ Working | Async read/write/sleep and epoll/timerfd `async_select()` |
| `Makefile` | ✅ Working | Library and test build targets |
| `test_100_fast.c` | ✅ Present | 100-goroutine fast scheduler test |
| `test_work_stealing.c` | ⚠️ Present | One run in the current audit exited 139; rerun before relying on it |
| `test_channel_integration.c` | ✅ Present | 100 sender/receiver channel test |
| `test_channel_no_blocking.c` | ✅ Present | Last run passed; checks other goroutines run while a receive is parked |
| `test_stack_overflow.c` | ✅ Present | Expected guard-page diagnostic and exit status 1 |
| `test_full_integration.c` | ⚠️ Present | One run in the current audit exited 139; rerun before relying on it |
| `test_async_select.c` | ✅ Present | fd readiness, timeout, and scheduler responsiveness |
| `test_async_goroutine.c` | ✅ Present | 50 asynchronous read tasks |
| `echo_server_goroutines.c` | ✅ Present | 100 concurrent local requests passed in this audit |
| `goroutine_lib.h`, `demo.c`, `goroutine_v2.c` | ⚠️ Legacy files | Old API/prototype sources remain in the repository |
| `scheduler.c`, `scheduler_v2.c` | ⚠️ Legacy files | Earlier scheduler implementations remain in the repository |
| `network_server.c` | ⚠️ Present, unverified | Not treated as a supported demo |

`async_accept()` is declared in `async_io.h`, but there is no implementation in
`async_io.c`. The fd-based `async_select()` does not select on channels.
Copying-stack growth is outside the current scope.

### Recommended Reading Order

1. `golight.h` and `golight.c` for the public entry points.
2. `worker_stealing.c` and `worker_stealing.h` for scheduling and context
   transitions.
3. `goroutine.c` and `goroutine.h` for stacks and context creation.
4. `channel.c` and `channel.h` for goroutine-aware channels.
5. `async_io.c` and `async_io.h` for async operations and fd selection.
6. `Makefile` for available build targets. Treat earlier prototypes as legacy.

## 5. Implementation Phases

P1–P5 are the project's completed implementation scope: scheduler park/wake and
async I/O integration; unified API; scheduler-aware channels; guarded goroutine
stacks; and fd-based `async_select()`.

### P1: Scheduler-Aware Async I/O

`park_goroutine()` uses `swapcontext()` and `scheduler_ready()` re-enqueues a
blocked goroutine. `async_read()` and `async_write()` use this path when an fd
is not immediately ready.

### P2: Unified API

`golight.h` exposes runtime lifecycle, `go()`, `yield()`, channel aliases,
goroutine IDs, WaitGroups, async I/O, and `async_select()`.

### P3: Scheduler-Aware Channels

Channel senders and receivers are queued while blocked. The implementation
releases the channel mutex before parking, and wakes queued goroutines through
the scheduler.

### P4: Stack Safety

Goroutine stacks are mmap-backed and have a guard page. A SIGSEGV on the guard
page reports the goroutine ID and exits with status 1. Stacks do not grow
dynamically.

### P5: fd-Based `async_select()`

`async_select()` registers fd read/write interests with epoll, uses a timerfd
for timeout, and parks a goroutine caller. It returns the ready case index or
`-1` on timeout. Channel cases are outside this API.

## 6. API Inventory

### 6.1 Unified Public API

Declared in `golight.h` and implemented in `golight.c`:

```c
#include "golight.h"

int runtime_init(void);
int go(void (*function)(void *), void *arg);
void runtime_wait(void);
void runtime_shutdown(void);
void yield(void);
uint64_t goroutine_id(void);

channel_t *make_channel(int capacity);
void chan_send(channel_t *channel, void *data);
void *chan_receive(channel_t *channel);
void chan_close(channel_t *channel);

waitgroup_t waitgroup_create(void);
void waitgroup_add(waitgroup_t group, int delta);
void waitgroup_done(waitgroup_t group);
void waitgroup_wait(waitgroup_t group);
void waitgroup_destroy(waitgroup_t group);
```

### 6.2 Async I/O API

Available through `golight.h` (declared by `async_io.h`):

```c
#include "golight.h"

future_t *async_read(int fd, void *buffer, size_t count);
future_t *async_write(int fd, const void *buffer, size_t count);
future_t *async_sleep(uint64_t milliseconds);
int async_select(async_select_case_t *cases, int ncases, uint64_t timeout_ms);
int future_wait(future_t *future);
int future_try_wait(future_t *future);
void future_free(future_t *future);
```

`async_accept()` is declared but has no function body. `async_select()` supports fd
read/write readiness and timeout, not channel selection.

### 6.3 Internal Scheduler and Channel API

The implementation also exposes lower-level functions in
`worker_stealing.h`, `goroutine.h`, `deque.h`, and `channel.h`, including
`park_goroutine()`, `scheduler_ready()`, and the `channel_*` functions. Most
applications should use the `golight.h` wrappers.

## 7. Key Algorithms

### 7.1 Scheduler Deques

The deque implementation provides bottom push/pop and top steal operations.
The scheduler wraps concurrent deque operations with mutexes; do not describe
the current overall scheduler path as lock-free.

### 7.2 Park and Resume

`park_goroutine()` saves the current goroutine context and switches back to the
worker context. A completion calls `scheduler_ready()`, which transitions a
blocked goroutine to ready and enqueues it on its assigned worker.

### 7.3 Channels

Channels protect buffer and waiter queue updates with a mutex. When a send or
receive must wait, the goroutine is added to the appropriate waiter queue,
unlocks, and parks. The matching operation wakes it through the scheduler.

### 7.4 Stack Guard

Each goroutine gets an mmap region with a `PROT_NONE` guard page below the usable
stack. Worker threads install an alternate signal stack so the SIGSEGV handler
can identify a guard-page overflow. This detects overflow; it does not grow the
stack.

### 7.5 Async Select

`async_select()` uses an epoll instance per call, duplicates the supplied fds
for registration lifetime, and registers a timerfd. The waiter completes with
the first fd case reported ready or the timeout result, then wakes the caller.

## 8. Build & Test Commands

### 8.1 Build the Library

```bash
make golight
```

The target builds `libgolight.a`.

### 8.2 Test Targets

```bash
make test_100_fast test_work_stealing test_channel_integration \
    test_channel_no_blocking test_stack_overflow test_full_integration \
    test_async_select
```

`test_async_goroutine` and `echo_server_goroutines` have source files but no
dedicated Makefile targets. Build them from their source with the listed
runtime sources when needed.

### 8.3 Latest Verification Notes

| Program | Observed result in current audit |
|---------|----------------------------------|
| `test_100_fast` | Exit 0; 100 completion lines |
| `test_channel_integration` | Exit 0; 100 sender and 100 receiver completion lines |
| `test_channel_no_blocking` | Exit 0; all ten ticks ran before the delayed sender |
| `test_stack_overflow` | Exit 1 with `STACK OVERFLOW detected in goroutine 1` |
| `test_full_integration` | Exit 139 in latest run; do not claim a reliable pass |
| `test_async_select` | Exit 0; readiness, timeout, and worker nonblocking checks passed |
| `test_async_goroutine` | Exit 0; completed 50/50 reads |
| `echo_server_goroutines` | Exit 0; handled 100/100 concurrent local clients |
| `test_work_stealing` | Exit 139 in latest run; do not claim a reliable pass |

### 8.4 Benchmark

`make bench` runs 10,000 no-op tasks at 1, 2, 4, and 8 workers. A single
observed run:

| Workers | Time | Throughput |
|---------|------|------------|
| 1 | 0.658 s | 15,208 tasks/s |
| 2 | 0.611 s | 16,362 tasks/s |
| 4 | 0.629 s | 15,900 tasks/s |
| 8 | 0.622 s | 16,069 tasks/s |

The result is environment- and run-dependent. Earlier 7.2×, 47 ns, 85 ns, and
95% figures are not supported by this benchmark run.

## 9. Known Issues & Gotchas

- The echo server uses port 8888; a stale server can cause an address-in-use
  error.
- Output from concurrent workers may interleave, and a terminal prompt may
  return only after the program's main thread has completed its wait.

## 10. What's Left

| Priority | Status |
|----------|--------|
| P1: Scheduler-aware async I/O | ✅ Complete |
| P2: Unified API and WaitGroups | ✅ Complete |
| P3: Scheduler-aware channels | ✅ Complete |
| P4: Guard-page stack overflow detection | ✅ Complete |
| P5: fd-based `async_select()` | ✅ Complete |
| P6: Cross-platform support | ⏸ Out of scope |
| P7: Profiling and observability | ⏸ Out of scope |

## 11. Design Decisions & Rationale

| Decision | Rationale |
|----------|-----------|
| `ucontext` goroutine contexts | Reuses the platform context API for cooperative switching |
| Worker-affine resumed contexts | Avoids resuming a started `ucontext` on a different pthread |
| Guard page and fatal overflow handler | Detects stack overflow without claiming stack growth |
| epoll for fd readiness | Linux event notification mechanism used by the runtime |
| timerfd for select timeout | Lets the select waiter block in epoll rather than poll |
| Cooperative scheduling | A goroutine must yield or block to allow other goroutines on that worker to run |

## 12. Literature / References

The design draws on work-stealing schedulers, CSP, and asynchronous I/O
literature. See [Literature Survey](./Literature%20Survey.md) for the survey
and references. These references provide context; they do not establish
GoLight-specific performance results.

## 13. How to Pick This Up (For Another AI)

### Reading Order

1. Read `golight.h` for the public API.
2. Read `golight.c` for runtime lifecycle, wrappers, and WaitGroup code.
3. Follow scheduler state transitions in `worker_stealing.c`.
4. Read `goroutine.c` for guarded stack setup and `ucontext` initialization.
5. Read `channel.c` and `async_io.c` for their park/wake paths.
6. Use the test source files and Makefile targets to reproduce checks.

**Suggested Next Task:** P1–P5 complete. To extend this project, consider:
cross-platform port (kqueue/IOCP), Prometheus metrics endpoint, execution
tracer, deadlock detector.

## 14. Honest Project Status Summary

### 14.1 Milestone Completion

| Milestone | Status |
|-----------|--------|
| P1: Scheduler-aware async I/O | ✅ Complete |
| P2: Unified runtime API | ✅ Complete |
| P3: Scheduler-aware channels | ✅ Complete |
| P4: Guard-page stack safety | ✅ Complete |
| P5: fd-based `async_select()` | ✅ Complete |
| P6: Cross-platform support | ⏸ Out of scope |
| P7: Profiling and observability | ⏸ Out of scope |

### 14.2 Overall Assessment

**Overall completion: ~95% of scoped work; 100% of P1–P5.** The P1–P5
features have implementation and focused test coverage. The latest audit also
observed failures in two broader tests (`test_work_stealing` and
`test_full_integration`), so those suites should not be described as reliably
passing until the failures are resolved and rerun.

### 14.3 End-to-End Entry Points

Build the static library with `make golight`, or build and run the focused
tests listed in Section 8. Run the goroutine echo server only after compiling
and testing its source in the current environment.

### 14.4 Scope Boundaries

GoLight is Linux-only and cooperative. Channel selection, copying-stack growth,
preemptive scheduling, cross-platform backends, and profiling are not part of
the current implementation scope.
