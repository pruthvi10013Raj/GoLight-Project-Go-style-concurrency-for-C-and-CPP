#ifndef GOROUTINE_LIB_H
#define GOROUTINE_LIB_H

// Main API - similar to Go's "go" keyword
void go(void (*func)(void*), void *arg);

// Channel API
typedef void* channel_t;
channel_t make_channel(int buffer_size);
void chan_send(channel_t ch, void *data);
void* chan_receive(channel_t ch);
void chan_close(channel_t ch);

// Wait group (simplified)
typedef struct waitgroup waitgroup_t;
waitgroup_t* create_waitgroup();
void waitgroup_add(waitgroup_t *wg, int delta);
void waitgroup_done(waitgroup_t *wg);
void waitgroup_wait(waitgroup_t *wg);

// Initialize the runtime
void goroutine_init();

#endif
