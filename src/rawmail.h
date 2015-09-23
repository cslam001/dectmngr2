
#ifndef RAWMAIL_H
#define RAWMAIL_H

#include <stdint.h>
#include "busmail.h"


int rawmail_send(void *_self, uint8_t *data, int size);
int rawmail_receive(void *_self, void *event);
int rawmail_dispatch(void *unused);
void rawmail_add_handler(void *_self , void (*app_handler)(packet_t *));
void* rawmail_new(int fd);

#endif
