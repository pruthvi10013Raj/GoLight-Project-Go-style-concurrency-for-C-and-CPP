# GoLight: Progress Report and Remaining Work

**Project:** GoLight — a cooperative user-level concurrency runtime for C
**Report updated:** 2026-11-05
**Implementation scope:** P1–P5 complete; P6 and P7 out of scope

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Timeline Overview](#2-timeline-overview)
3. [Implementation Progress](#3-implementation-progress)
4. [What Works Today](#4-what-works-today)
5. [Previously Partial Areas](#5-previously-partial-areas)
6. [Out-of-Scope Work](#6-out-of-scope-work)
7. [Completion Matrix](#7-completion-matrix)
8. [Remaining Work — Prioritized](#8-remaining-work--prioritized)
9. [File-by-File Status](#9-file-by-file-status)
10. [Command Reference](#10-command-reference)
11. [Next Milestone Recommendation](#11-next-milestone-recommendation)
12. [What We Can Confidently Claim](#appendix-a-what-we-can-confidently-claim)

## 1. Executive Summary

GoLight provides cooperative goroutines, a pthread-backed scheduler, channels,
WaitGroups, async fd I/O, guard-page stack overflow detection, and fd-based
`async_select()`, exposed primarily through `golight.h`.

P1–P5 are marked complete. This report does not claim that every broader
stress-test binary is stable: the latest documentation audit observed exit
139 from `test_work_stealing` and `test_full_integration`. Focused async-select,
async-I/O, channel, and 100-goroutine tests have passed in recorded runs.

## 2. Timeline Overview

Completion dates are listed only where recorded in the available session
history. Earlier P1/P2 dates were not recorded in the repository.

| Priority | Scope | Status | Completion date evidence |
|----------|-------|--------|--------------------------|
| P1 | Scheduler-aware async I/O | ✅ Done | Date not recorded in repository |
| P2 | Unified API and WaitGroups | ✅ Done | Date not recorded in repository |
| P3 | Scheduler-aware channels | ✅ Done | 2026-11-04 verification |
| P4 | mmap stacks and guard-page overflow detection | ✅ Done | 2026-11-04 verification |
| P5 | fd-based `async_select()` | ✅ Done | 2026-11-05 verification |
| P6 | Cross-platform support | ⏸ Out of scope | — |
| P7 | Profiling and observability | ⏸ Out of scope | — |

## 3. Implementation Progress

### 3.1 Goroutines and Scheduling

Goroutines use `ucontext` and mmap-backed stacks. The scheduler has worker
queues and park/ready transitions. Resumed goroutines remain on their assigned
worker thread.

### 3.2 Channels and Unified API

`golight.h` exposes `go()`, runtime lifecycle calls, yield, channel aliases,
goroutine IDs, and WaitGroups. Channel wait queues park goroutines instead of
blocking workers.

### 3.3 Async I/O and Selection

Async read/write use scheduler parking when fd operations would block.
`async_select()` registers read/write fd cases with epoll and uses timerfd for
timeouts. It does not select on channels. `async_accept()` is declared but has
no implementation.

### 3.4 Stack Safety

Each goroutine stack has an mmap-backed guard page. A stack overflow fault is
reported with the goroutine ID and terminates the process with status 1.
Copying-stack growth is outside the current scope.

## 4. What Works Today

The following results were observed in the current worktree or recent session
verification:

| Check | Result |
|-------|--------|
| `make -B golight` | Completed without compiler warnings |
| `test_100_fast` | Exit 0; 100 completion lines |
| `test_channel_integration` | Exit 0; 100 sender and 100 receiver completions in recorded run |
| `test_channel_no_blocking` | Exit 0; all ten ticks ran before the delayed sender |
| `test_stack_overflow` | Expected diagnostic; exit 1 |
| `test_async_select` | Readiness, timeout, and worker-responsiveness checks passed |
| `test_async_goroutine` | Exit 0; 50/50 reads |
| `echo_server_goroutines` | Exit 0; 100/100 concurrent local clients |
| `test_work_stealing` | Latest audit run exited 139 |
| `test_full_integration` | Latest audit run exited 139 |

## 5. Previously Partial Areas

These features were previously missing or incomplete and now have
implementations:

| Area | Current implementation |
|------|-------------------------|
| Goroutine parking and wakeup | `swapcontext()` and scheduler deque re-enqueue |
| Async read/write integration | Park on unavailable fd readiness |
| Unified API | `golight.h` and `golight.c` runtime wrappers |
| Channels | Goroutine waiter queues and scheduler wakeup |
| Stack overflow detection | mmap guard page and SIGSEGV handler |
| fd selection | `async_select()` with epoll and timerfd |
| Static library | `make golight` builds `libgolight.a` |

## 6. Out-of-Scope Work

| Feature | Status |
|---------|--------|
| macOS kqueue and Windows IOCP backends | ⏸ Out of scope |
| Built-in profiler, metrics endpoint, and execution tracer | ⏸ Out of scope |
| Preemptive scheduling | Not provided; scheduling is cooperative |
| Copying-stack growth | Not implemented |
| Selection across channels | Not implemented |
| `async_accept()` implementation | Declared only |

## 7. Completion Matrix

| Priority / feature | Implementation | Focused evidence | Status |
|--------------------|----------------|------------------|--------|
| P1: Scheduler-aware async I/O | Present | `test_async_goroutine` 50/50 run | ✅ Complete |
| P2: Unified API and WaitGroups | Present | `test_full_integration` source; latest execution failed | ✅ Complete, broad test needs rerun |
| P3: Scheduler-aware channels | Present | `test_channel_integration` and `test_channel_no_blocking` | ✅ Complete |
| P4: Guard-page stack safety | Present | `test_stack_overflow` expected diagnostic | ✅ Complete |
| P5: fd-based `async_select()` | Present | `test_async_select` three checks | ✅ Complete |
| P6: Cross-platform support | Not implemented | Linux epoll dependency | ⏸ Out of scope |
| P7: Profiling and observability | Not implemented | No profiling endpoint found | ⏸ Out of scope |

## 8. Remaining Work — Prioritized

| Priority | Status | Note |
|----------|--------|------|
| P1 | ✅ Done | Completion date not recorded in repository |
| P2 | ✅ Done | Completion date not recorded in repository |
| P3 | ✅ Done | Verified 2026-11-04 |
| P4 | ✅ Done | Verified 2026-11-04 |
| P5 | ✅ Done | Verified 2026-11-05 |
| P6 | ⏸ Out of scope | Cross-platform backends |
| P7 | ⏸ Out of scope | Profiling and observability |

**Minimum viable completion (P1+P2+P3+P4+P5) — ACHIEVED.**

The two stress tests that exited 139 during the latest audit should be
investigated before making a claim that the entire suite is consistently
passing. This report records the observed result rather than hiding it.

## 9. File-by-File Status

### 9.1 Public API and Core Runtime

| File | Status | Notes |
|------|--------|-------|
| `golight.h` | ✅ Working | Public API declarations |
| `golight.c` | ✅ Working | Lifecycle, wrappers, WaitGroup |
| `goroutine.h` | ✅ Working | Goroutine metadata and declarations |
| `goroutine.c` | ✅ Working | Context setup and guarded stacks |
| `worker_stealing.h` | ✅ Working | Scheduler declarations |
| `worker_stealing.c` | ✅ Working | Dispatch, park, yield, and ready operations |
| `deque.h`, `deque.c` | ✅ Working | Deque implementation; scheduler locks concurrent operations |
| `channel.h`, `channel.c` | ✅ Working | Buffered/unbuffered channels with parked waiters |
| `async_io.h` | ✅ Working | Async API declarations |
| `async_io.c` | ✅ Working | Read/write/sleep and fd-based `async_select()` |
| `Makefile` | ✅ Working | Library and focused test targets |

### 9.2 Legacy or Unverified Sources

| File | Status | Notes |
|------|--------|-------|
| `goroutine_lib.h`, `demo.c`, `goroutine_v2.c` | ⚠️ Legacy | Remain in the repository; superseded by `golight.h` |
| `scheduler.c`, `scheduler_v2.c` and headers | ⚠️ Legacy | Earlier scheduler prototypes |
| `network_server.c` | ⚠️ Unverified | Not a supported or verified example |
| `async_accept()` | ⚠️ Declaration only | No function body found |

### 9.3 Current Test Sources

| File | Status / behavior |
|------|-------------------|
| `test_100_fast.c` | Last run: 100 goroutines completed |
| `test_work_stealing.c` | Latest audit run exited 139 |
| `test_channel_integration.c` | 100 sender/receiver integration test |
| `test_channel_no_blocking.c` | Last run passed; checks progress during blocked channel receive |
| `test_stack_overflow.c` | Expected guard-page diagnostic and exit 1 |
| `test_full_integration.c` | Latest audit run exited 139 |
| `test_async_select.c` | Readiness, timeout, and scheduler responsiveness |
| `test_async_goroutine.c` | Last run completed 50 asynchronous reads |
| `echo_server_goroutines.c` | Last local load test handled 100/100 clients |

## 10. Command Reference

### Build

```bash
make golight
```

### Build Focused Tests

```bash
make test_100_fast test_work_stealing test_channel_integration \
    test_channel_no_blocking test_stack_overflow test_full_integration \
    test_async_select
```

`test_async_goroutine` and `echo_server_goroutines` do not have Makefile
targets in the current Makefile. Build them by linking their source with the
runtime sources.

### Benchmark

```bash
make bench
```

One recorded run measured the following 10,000 no-op task throughput. Results
vary by environment:

| Workers | Time | Throughput |
|---------|------|------------|
| 1 | 0.658 s | 15,208 tasks/s |
| 2 | 0.611 s | 16,362 tasks/s |
| 4 | 0.629 s | 15,900 tasks/s |
| 8 | 0.622 s | 16,069 tasks/s |

## 11. Next Milestone Recommendation

P1–P5 are complete. Before submission, rerun and investigate the latest
`test_work_stealing` and `test_full_integration` failures. Further work in the
current scope should focus on making their results reliable and repeatable.
Afterward, potential extensions are cross-platform backends, metrics, tracing,
and deadlock detection.

## Appendix A: What We Can Confidently Claim

### Safe to Claim

| Claim | Evidence / qualification |
|-------|--------------------------|
| Guard-page stack overflow detection exists | `test_stack_overflow` prints a goroutine-specific diagnostic and exits 1 |
| fd-based `async_select()` is implemented | `test_async_select` readiness, timeout, and worker-progress checks |
| Fully integrated async read/write paths | Async read/write use scheduler park/ready; `test_async_goroutine` recorded 50/50 reads |
| CSP channels work with goroutines | Channel waiter queues; `test_channel_integration` and `test_channel_no_blocking` passed |
| Unified API is available | `golight.h` and `golight.c`; `make golight` |
| 100 fast goroutines can complete | `test_100_fast` recorded 100 completion lines |

### Claim with Caveats

| Claim | Caveat |
|-------|--------|
| Full end-to-end integration passes | Latest `test_full_integration` run exited 139 |
| Work-stealing stress test passes | Latest `test_work_stealing` run exited 139 |
| Benchmark scales to 8 workers | A single measured run showed about 1.06× throughput over one worker, not a 7.2× speedup |

### Do Not Claim

- Do not claim preemptive scheduling, cross-platform support, channel
  selection, dynamic stack growth, or an `async_accept()` implementation.
- Do not claim 7.2× speedup, 47 ns context switches, 85 ns goroutine creation,
  or 95% CPU utilization based on the current benchmark evidence.
- Do not describe the complete stress-test suite as reliably passing until the
  two observed exit-139 results have been resolved and rerun.
