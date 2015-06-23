#include <stdlib.h>

#include "stream.h"
#include "util.h"



typedef struct {
	int fd;
	void (*event_handler)(void *e);
} stream_t;

void * stream_new(int fd) {
	
	stream_t * s = (stream_t *) calloc( sizeof(stream_t), 1);
	
	s->fd = fd;

	return s;
}


int stream_get_fd(void * _self) {
	
	stream_t * s = (stream_t *) _self;
	
	return s->fd;
}


void * stream_get_handler(void * _self) {
	
	stream_t * s = (stream_t *) _self;
	
	return s->event_handler;
}


void * stream_add_handler(void * _self, void (*event_handler)(void *stream, void * event)) {
	
	stream_t * s = (stream_t *) _self;
	
	s->event_handler = event_handler;
	
	return;
}


void * stream_remove_handler(void * _self, void (*event_handler)(void *stream, void * event)) {
	
	stream_t * s = (stream_t *) _self;
	
	s->event_handler = NULL;
	
	return;
}


