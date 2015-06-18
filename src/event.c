#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>

#include "stream.h"
#include "error.h"

#define BUF_SIZE 5000


typedef struct event {
	uint8_t *in;
	int count;
} event_t;


void * event_new(void * stream) {
	
	event_t * e = (event_t *) calloc(sizeof(event_t), 1);
	if (!e) err_exit("calloc");
	
	e->in = (uint8_t *) calloc(BUF_SIZE, 1);
	if (!e->in) err_exit("calloc");
	
	e->count = read(stream_get_fd(stream), e->in, BUF_SIZE);

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
