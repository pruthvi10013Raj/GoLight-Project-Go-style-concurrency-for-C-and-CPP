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

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

debug: CFLAGS = $(DEBUG_FLAGS) -pthread -D_GNU_SOURCE
debug: test_work_stealing

clean:
	rm -f $(TARGETS) *.o

run: test_work_stealing
	./test_work_stealing

bench: benchmark
	./benchmark

.PHONY: all clean run bench debug golight
