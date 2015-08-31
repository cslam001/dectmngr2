#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>

#include "event.h"
#include "error.h"


typedef struct event {
	uint8_t *in;
	int count;
} event_t;


void * event_new(stream_t *stream) {
	
	event_t * e = (event_t *) calloc(sizeof(event_t), 1);
	if (!e) err_exit("calloc");

	if(stream->maxEventSize > 0) {	
		e->in = (uint8_t *) calloc(stream->maxEventSize, 1);
		if (!e->in) err_exit("calloc");
	
		e->count = read(stream_get_fd(stream), e->in, stream->maxEventSize);
	}

	return e;
}


void event_destroy(void * _self) {
	
	event_t * e = (event_t *) _self;

	free(e->in);
	free(e);
}


uint8_t * event_data(void * _self) {

	event_t * e = (event_t *) _self;
	
	return e->in;
}


int event_count(void * _self) {

	event_t * e = (event_t *) _self;

	return e->count;
}
