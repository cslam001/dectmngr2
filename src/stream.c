#include <stdlib.h>

#include "stream.h"
#include "util.h"



void * stream_new(int fd) {
	
	stream_t * s = (stream_t *) calloc( sizeof(stream_t), 1);
	
	s->fd = fd;

	return (void*) s;
}


int stream_get_fd(void * _self) {
	
	stream_t * s = (stream_t *) _self;
	
	return s->fd;
}


void * stream_get_handler(void * _self) {
	
	stream_t * s = (stream_t *) _self;
	
	return s->event_handler;
}



// Register a handler for events arriving through
// a stream.
// Args: _self         = Pointer to a stream allocated with stream_new()
//       maxEventSize  = Size of events the event handler can cope
//       event_handler = Function pointer to callback
void * stream_add_handler(void * _self, int maxEventSize, void (*event_handler)(void *stream, void * event)) {
	
	stream_t * s = (stream_t *) _self;

	s->maxEventSize = maxEventSize;	
	if(s->maxEventSize > MAX_EVENT_SIZE) s->maxEventSize = MAX_EVENT_SIZE; 
	s->event_handler = event_handler;
	
	return;
}


void * stream_remove_handler(void * _self, void (*event_handler)(void *stream, void * event)) {
	
	stream_t * s = (stream_t *) _self;
	
	s->event_handler = NULL;
	
	return;
}


