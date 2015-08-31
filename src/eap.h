#ifndef EAP_H
#define EAP_H

#include <stdint.h>
#include "buffer.h"
#include "busmail.h"

#define PACKET_SIZE 500
#define NO_PF 0
#define PF 1


/* typedef struct __attribute__((__packed__))  */
/* { */
/* 	uint8_t frame_header; */
/* 	uint8_t program_id; */
/* 	uint8_t task_id; */
/* 	uint16_t mail_header; */
/* 	uint8_t mail_data[1]; */
/* } eap_t; */



void* eap_new(int fd, void (*app_handler)(packet_t *));
int eap_write(void * _self, void * event);
void eap_dispatch(void * _self);
void eap_send(void * _self, uint8_t * data, int size);

#endif /* EAP_H */
