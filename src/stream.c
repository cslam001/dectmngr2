#include <stdlib.h>

#include "stream.h"


typedef struct {
	int fd;
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

