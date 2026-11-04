# GoLight-Project-Go-style-concurrency-for-C-and-CPP
Bringing Go-style concurrency for C/C++ Systems

Modern server applications must manage tens of thousands of concurrent connections, but traditional C/C++ threading models do not scale efficiently due to high memory usage and scheduling overhead. Unlike languages such as Go, C and C++ lack built-in lightweight concurrency mechanisms, making it difficult to build highly concurrent systems.
To address this problem, we present GoLight, an open-source library that provides lightweight user-level threads with M:N scheduling for C and C++ applications. GoLight enables goroutine-like tasks, efficient scheduling across CPU cores, and safe communication between tasks using channels. It also integrates seamlessly with blocking system calls through asynchronous I/O support. By offering these features as a simple, dependency-free library, GoLight makes scalable concurrency practical and accessible for systems programming in C and C++.

## Runtime API

The unified C API is declared in `golight.h`. Build the static library with
`make libgolight.a`, then link an application with `-L. -lgolight -pthread`.

```c
#include "golight.h"

static void task(void *arg) {
    /* Run concurrently on a scheduler worker. */
}

int main(void) {
    if (runtime_init() != 0) return 1;
    if (go(task, NULL) != 0) {
        runtime_shutdown();
        return 1;
    }
    runtime_wait();
    runtime_shutdown();
    return 0;
}
```

`make_channel()` creates the existing buffered channel implementation;
`channel_send()` / `channel_receive()` and `async_read()` are available through
the same header. Channel operations still use pthread condition variables, so
they can block a scheduler worker. `async_read()` parks a running goroutine when
the descriptor would block and resumes it when the I/O reactor reports readiness.
