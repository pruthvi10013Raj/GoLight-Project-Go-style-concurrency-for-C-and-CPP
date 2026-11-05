# Requirements Analysis

This document distinguishes implemented behavior from aspirational performance
targets. Targets below are not claims of measured GoLight performance.

## 1.1 Functional Requirements

### Core Requirements

| ID | Requirement | Priority | Current status |
|----|-------------|----------|----------------|
| F1 | Goroutine creation and cooperative context switching | P0 | Implemented with `ucontext`; guarded mmap stacks |
| F2 | M:N scheduling | P0 | Implemented with pthread workers and user-level goroutines |
| F3 | Work distribution | P0 | Worker deques and stealing paths exist; `test_work_stealing` exited 139 in the latest audit run |
| F4 | Channel communication | P0 | Buffered and unbuffered channels park goroutines; channel selection is outside current scope |
| F5 | Async I/O | P1 | Async read/write and fd-based `async_select()` use scheduler parking |
| F6 | Wait groups | P1 | Implemented in the unified `golight.h` API |
| F7 | Profiling and metrics | P7 | Out of scope for this version |

## 1.2 Non-Functional Requirements

### Performance Requirements

These are historical design targets only. They have not been established by the
current benchmark and must not be presented as achieved measurements.

| Metric | Historical target | Verification status |
|--------|-------------------|---------------------|
| Goroutine creation time | <100 ns | Not measured by current benchmark |
| Context-switch latency | <50 ns | Not measured by current benchmark |
| Uncontended channel send | <25 ns | Not measured |
| Memory per goroutine | <2 KB | Not measured; current usable stack is 64 KB plus guard page |
| Maximum goroutine count | >1,000,000 | Not tested |
| CPU utilization | >95% | Not measured |
| Tail latency (p99) | <2× mean | Not measured |

The current benchmark schedules 10,000 no-op tasks over 1, 2, 4, and 8 workers.
One recorded run reported 15,208, 16,362, 15,900, and 16,069 tasks/second
respectively. This single run is not a performance guarantee and does not
support the older 7.2× speedup claim.

### System Requirements

| Category | Current scope |
|----------|---------------|
| OS | Linux; async fd readiness uses epoll and select timeout uses timerfd |
| Scheduling | Cooperative; goroutines need to yield or park |
| Context | `ucontext` |
| C compiler | GCC-compatible toolchain with pthread support |
| Cross-platform backends | Not in scope; no kqueue or IOCP implementation |

## 1.3 Use Cases

### Use Case 1: High-Concurrency Server

The goroutine-based echo server source demonstrates per-client goroutine
handling. Build and load-test it in the target environment before making a
concurrency or throughput claim.

### Use Case 2: Data Processing Pipeline

The runtime provides channels for goroutine-to-goroutine message passing and
WaitGroups for completion tracking. Channel selection across multiple channels
is not provided.
