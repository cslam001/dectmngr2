#ifndef STREAM_H
#define STREAM_H

#include "util.h"

void * stream_new(int fd);
int stream_get_fd(void * _self);
void * stream_get_handler(void * _self);
void * stream_add_handler(void * _self, void (*event_handler)(event_t *e));












#endif /* STREAM_H */
