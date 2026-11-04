#ifndef GOLIGHT_H
#define GOLIGHT_H

#include "async_io.h"
#include "channel.h"
#include "goroutine.h"
#include <stdint.h>

int runtime_init(void);
int go(void (*function)(void*), void* arg);
channel_t* make_channel(int capacity);
void runtime_wait(void);
void runtime_shutdown(void);

void yield(void);
uint64_t goroutine_id(void);

void chan_send(channel_t* ch, void* data);
void* chan_receive(channel_t* ch);
void chan_close(channel_t* ch);

typedef struct waitgroup* waitgroup_t;
waitgroup_t waitgroup_create(void);
void waitgroup_add(waitgroup_t wg, int delta);
void waitgroup_done(waitgroup_t wg);
void waitgroup_wait(waitgroup_t wg);
void waitgroup_destroy(waitgroup_t wg);

#endif
