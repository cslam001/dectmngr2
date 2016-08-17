#ifndef EAP_H
#define EAP_H

#include <stdint.h>
#include "buffer.h"
#include "busmail.h"


typedef struct {
	uint32_t fd;
	uint8_t tx_seq_l;
	uint8_t rx_seq_l;
	uint8_t tx_seq_r;
	uint8_t rx_seq_r;
	void * tx_fifo;
	buffer_t * buf;
	uint8_t program_id;
	uint8_t task_id;
	void (*application_frame) (packet_t *);
} eap_connection_t;


eap_connection_t* eap_new(int fd, void (*app_handler)(packet_t *));
int eap_write(eap_connection_t *bus, void * event);
void eap_dispatch(eap_connection_t *bus);
void eap_send(eap_connection_t *bus, uint8_t * data, int size);

#endif /* EAP_H */
