#ifndef EVENT_H
#define EVENT_H

#include "stream.h"


void * event_new(stream_t *stream);
void event_destroy(void * event);
uint8_t * event_data(void * event);
int event_count(void * event);


#endif /* EVENT_H */



