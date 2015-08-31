#ifndef STREAM_H
#define STREAM_H

#include <bits/uClibc_page.h>
#include "util.h"

#define MAX_EVENT_SIZE		PAGE_SIZE

typedef struct {
	int fd;
	int maxEventSize;
	void (*event_handler)(void *stream, void * event);
} stream_t;



void * stream_new(int fd);
int stream_get_fd(void * _self);
void * stream_get_handler(void * _self);
void stream_add_handler(void * _self, int maxEventSize, void (*event_handler)(void *stream, void * event));
void stream_remove_handler(void * _self, void (*event_handler)(void *stream, void * event));












#endif /* STREAM_H */
