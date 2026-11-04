CC = gcc
CFLAGS = -Wall -O2 -pthread -D_GNU_SOURCE
DEBUG_FLAGS = -g -O0 -DDEBUG

TARGETS = test_work_stealing benchmark libgolight.a

all: $(TARGETS)

test_work_stealing: test_work_stealing.c worker_stealing.c deque.c goroutine.c
	$(CC) $(CFLAGS) -o $@ $^

benchmark: benchmark.c worker_stealing.c deque.c goroutine.c
	$(CC) $(CFLAGS) -o $@ $^

libgolight.a: golight.o async_io.o channel.o worker_stealing.o deque.o goroutine.o
	ar rcs $@ $^

golight: libgolight.a

test_channel_integration: test_channel_integration.c golight.c worker_stealing.c deque.c goroutine.c channel.c async_io.c
	$(CC) $(CFLAGS) -o $@ $^

test_channel_no_blocking: test_channel_no_blocking.c golight.c worker_stealing.c deque.c goroutine.c channel.c async_io.c
	$(CC) $(CFLAGS) -o $@ $^

test_stack_overflow: test_stack_overflow.c worker_stealing.c deque.c goroutine.c
	$(CC) $(CFLAGS) -o $@ $^

test_100_fast: test_100_fast.c worker_stealing.c deque.c goroutine.c
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

debug: CFLAGS = $(DEBUG_FLAGS) -pthread -D_GNU_SOURCE
debug: test_work_stealing

clean:
	rm -f $(TARGETS) test_channel_integration test_channel_no_blocking test_stack_overflow test_100_fast *.o

run: test_work_stealing
	./test_work_stealing

bench: benchmark
	./benchmark

.PHONY: all clean run bench debug golight
