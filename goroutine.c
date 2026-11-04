#include "goroutine.h"
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>

#define GUARD_PAGE_SIZE 4096
#define ALT_STACK_SIZE (64 * 1024)

static int next_id = 1;
static __thread goroutine_t* active_goroutine;
static __thread char alternate_signal_stack[ALT_STACK_SIZE];
static pthread_once_t segv_handler_once = PTHREAD_ONCE_INIT;

static void write_decimal(int value, char* buffer, size_t* length) {
    char digits[16];
    size_t count = 0;
    unsigned int number = value < 0 ? (unsigned int)(-value) : (unsigned int)value;
    do {
        digits[count++] = (char)('0' + number % 10);
        number /= 10;
    } while (number && count < sizeof(digits));
    if (value < 0) buffer[(*length)++] = '-';
    while (count) buffer[(*length)++] = digits[--count];
}

static void segv_handler(int signal_number, siginfo_t* info, void* context) {
    (void)context;
    goroutine_t* g = active_goroutine;
    uintptr_t fault = (uintptr_t)info->si_addr;
    uintptr_t guard_start = g ? (uintptr_t)g->stack_mapping : 0;
    uintptr_t guard_end = g ? guard_start + g->guard_page_size : 0;

    if (g && fault >= guard_start && fault < guard_end) {
        char message[96] = "STACK OVERFLOW detected in goroutine ";
        size_t length = sizeof("STACK OVERFLOW detected in goroutine ") - 1;
        write_decimal(g->id, message, &length);
        message[length++] = '\n';
        ssize_t written = write(STDERR_FILENO, message, length);
        (void)written;
        _exit(1);
    }

    (void)signal(signal_number, SIG_DFL);
    (void)raise(signal_number);
    _exit(128 + signal_number);
}

static void install_segv_handler(void) {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_sigaction = segv_handler;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGSEGV, &action, NULL) != 0) {
        perror("sigaction(SIGSEGV)");
        abort();
    }
}

static void* alloc_stack(size_t size, void** mapping, size_t* mapping_size) {
    if (size > SIZE_MAX - GUARD_PAGE_SIZE) {
        errno = EOVERFLOW;
        return NULL;
    }
    size_t total = GUARD_PAGE_SIZE + size;
    void* memory = mmap(NULL, total, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) return NULL;
    if (mprotect(memory, GUARD_PAGE_SIZE, PROT_NONE) != 0) {
        int saved_errno = errno;
        munmap(memory, total);
        errno = saved_errno;
        return NULL;
    }
    *mapping = memory;
    *mapping_size = total;
    return (char*)memory + GUARD_PAGE_SIZE;
}

int goroutine_thread_init(void) {
    if (pthread_once(&segv_handler_once, install_segv_handler) != 0) {
        errno = EINVAL;
        return -1;
    }
    stack_t stack = {
        .ss_sp = alternate_signal_stack,
        .ss_size = sizeof(alternate_signal_stack),
        .ss_flags = 0,
    };
    return sigaltstack(&stack, NULL);
}

void goroutine_set_active(goroutine_t* g) {
    active_goroutine = g;
}

static void goroutine_wrapper(goroutine_t *g) {
    g->function(g->arg);
    atomic_store(&g->state, GOROUTINE_FINISHED);
    if (g->return_context) {
        if (setcontext(g->return_context) == -1) {
            perror("setcontext after goroutine completion");
            abort();
        }
    }
}

goroutine_t* create_goroutine(void (*func)(void*), void *arg) {
    goroutine_t * volatile g = malloc(sizeof(goroutine_t));
    if (!g) return NULL;
    
    g->id = next_id++;
    g->function = func;
    g->arg = arg;
    g->stack_mapping = NULL;
    g->stack_mapping_size = 0;
    g->guard_page_size = GUARD_PAGE_SIZE;
    g->worker_id = -1;
    g->return_context = NULL;
    g->wait_next = NULL;
    g->wait_channel = NULL;
    g->wait_value = NULL;
    g->wait_value_ready = 0;
    atomic_init(&g->state, GOROUTINE_READY);
    g->stack_size = 64 * 1024;
    g->stack = alloc_stack(g->stack_size, &g->stack_mapping,
                           &g->stack_mapping_size);
    
    if (!g->stack) {
        free(g);
        return NULL;
    }
    
    // Initialize context
    getcontext(&g->context);
    g->context.uc_stack.ss_sp = g->stack;
    g->context.uc_stack.ss_size = g->stack_size;
    g->context.uc_link = NULL;  // Will be set by scheduler when running
    
    // Create context that calls wrapper with goroutine as argument
    makecontext(&g->context, (void(*)(void))goroutine_wrapper, 1, g);
    
    printf("Created goroutine %d\n", g->id);
    return g;
}

void run_goroutine(goroutine_t *g) {
    if (g->state == GOROUTINE_READY) {
        g->state = GOROUTINE_RUNNING;
        printf("Starting goroutine %d\n", g->id);
        setcontext(&g->context);
    }
}

void free_goroutine(goroutine_t *g){
    if (g) {
        if (g->stack_mapping) munmap(g->stack_mapping, g->stack_mapping_size);
        free(g);
    }
}
