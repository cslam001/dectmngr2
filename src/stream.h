#ifndef STREAM_H
#define STREAM_H

void * stream_new(int fd);
int stream_get_fd(void * _self);
void * stream_get_handler(void * _self);












#endif /* STREAM_H */
