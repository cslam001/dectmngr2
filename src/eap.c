#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#include "error.h"
#include "busmail.h"
#include "fifo.h"
#include "state.h"
#include "event.h"

#define EAP_PROG_ID 0x81
#define RSX_TSK_ID 0xdc


typedef struct {
	uint8_t * data;
	int size;
	uint8_t task_id;
} tx_packet_t;


#define CLIENT_PKT_DATA_SIZE 1000
#define CLIENT_PKT_TYPE 5
#define CLIENT_PKT_HEADER_SIZE 8

typedef struct {
	uint32_t size;
	uint32_t type;
	uint8_t data[CLIENT_PKT_DATA_SIZE];
} client_packet_t;


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
} busmail_connection_t;


/* Module scope variables */
/* static uint8_t tx_seq_l, rx_seq_l, tx_seq_r, rx_seq_r; */
/* static int busmail_fd; */
/* static void (*application_frame) (busmail_t *); */
extern void * client_list;
client_packet_t client_p;


static void reset_counters(void * _self) {

	busmail_connection_t * bus = (busmail_connection_t *) _self;

	bus->tx_seq_l = 0;
	bus->rx_seq_l = 0;
	bus->tx_seq_r = 0;
	bus->rx_seq_r = 0;

	return;
}


static uint8_t * make_tx_packet(uint8_t * tx, void * packet, int data_size) {
  
  uint8_t * data = (uint8_t *) packet;
  int i;
  uint8_t crc = 0;
  
  tx[0] = BUSMAIL_PACKET_HEADER;
  tx[1] = (uint8_t) (data_size >> 8);
  tx[2] = (uint8_t) data_size;

  /* Calculate checksum over data portion */
  for (i = 0; i < data_size; i++) {
	  crc += data[i];
	  tx[BUSMAIL_PACKET_OVER_HEAD - 1 + i] = data[i];
  }

  
  tx[BUSMAIL_PACKET_OVER_HEAD - 1 + data_size] = crc;

  
  return tx;
}



static void send_packet(void * data, int data_size, int fd) {

  int tx_size = data_size + BUSMAIL_PACKET_OVER_HEAD + BUSMAIL_HEADER_SIZE;
  uint8_t * tx = malloc(tx_size);
  
  make_tx_packet(tx, data, data_size + BUSMAIL_HEADER_SIZE);
  util_dump(tx, tx_size, "[WRITE to client]");
  write(fd, tx, tx_size);
  free(tx);
}




static uint8_t make_info_frame(void * _self) {
	
	busmail_connection_t * bus = (busmail_connection_t *) _self;
	uint8_t header;

	header = ( (bus->tx_seq_l << TX_SEQ_OFFSET) | 
		(bus->rx_seq_l << RX_SEQ_OFFSET));

	return header;
}



static void eap_tx(void * _self, uint8_t * data, int size, uint8_t task_id) {

	busmail_connection_t * bus = (busmail_connection_t *) _self;
	uint8_t tx_seq_tmp, rx_seq_tmp;
	busmail_t * r;	
	
	r = malloc(BUSMAIL_PACKET_OVER_HEAD - 1 + size);
	if (!r) {
		exit_failure("malloc");
	}

		
	r->frame_header = make_info_frame(bus);
	r->program_id = bus->program_id;
	r->task_id = task_id;
	memcpy(&r->mail_header, data, size);

	send_packet(r, size, bus->fd);

	// Always send a sniff copy to RSX debugger too
	if(r->program_id != EAP_PROG_ID) {
		r->program_id = EAP_PROG_ID;
		r->task_id = RSX_TSK_ID;
		send_packet(r, size, bus->fd);
	}

	free(r);
}


/* Send an EAP busmail where the addressee has
 * been specified by previously received packet. */
void eap_send(void * _self, uint8_t * data, int size) {

	busmail_connection_t * bus = (busmail_connection_t *) _self;
	tx_packet_t * tx = calloc(sizeof(tx_packet_t), 1);
	
	tx->data = malloc(size - BUSMAIL_HEADER_SIZE);
	memcpy(tx->data, data + BUSMAIL_HEADER_SIZE, size - BUSMAIL_HEADER_SIZE);
	tx->task_id = data[TASK_ID_OFFSET];
	tx->size = size - BUSMAIL_HEADER_SIZE;
	
	eap_tx(bus, tx->data, tx->size, tx->task_id);
	free(tx);
}


static void information_frame(void * _self, packet_t *p) {

	busmail_connection_t * bus = (busmail_connection_t *) _self;
	busmail_t * m = (busmail_t *) &p->data[0];
	uint8_t sh, ih;
	int ack = true;


	/* Update busmail packet counters */
	bus->tx_seq_r = (m->frame_header & TX_SEQ_MASK) >> TX_SEQ_OFFSET;
	bus->rx_seq_r = (m->frame_header & RX_SEQ_MASK) >> RX_SEQ_OFFSET;
	bus->program_id = m->program_id;
	bus->task_id = m->task_id;

	util_dump(p->data, p->size, "[CLIENT]");

	/* Set next pair of packet sequence numbers
	 * to whatever remote want. We don't implement
	 * resending (yet) and thus the numbers are
	 * of minor importance. */
	bus->tx_seq_l = bus->rx_seq_r;
	bus->rx_seq_l = bus->tx_seq_r + 1;
	bus->rx_seq_l &= 15u;							// Wrap to 0 at 16

	/* Process application frame. The application frame callback will enqueue 
	   outgoing packets on tx_fifo and directly transmit packages with busmail_send() */
	bus->application_frame(p);
}



int eap_write(void * _self, void * event) {

	busmail_connection_t * bus = (busmail_connection_t *) _self;
	
	if ( buffer_write(bus->buf, event_data(event), event_count(event)) == 0 ) {
		return -1;
	}
	
	return 0;
}


void eap_dispatch(void * _self) {

	busmail_connection_t * bus = (busmail_connection_t *) _self;
	packet_t packet;
	packet_t *p = &packet;
	busmail_t * m;
	
	/* Process whole packets in buffer */
	while ( busmail_get(bus, p) == 0) {

		/* No control frames in EAP */
		information_frame(bus, p);

	}
}



void* eap_new(int fd, void (*app_handler)(packet_t *)) {

	busmail_connection_t * bus = (busmail_connection_t *) calloc(sizeof(busmail_connection_t), 1);

	bus->fd = fd;
	bus->application_frame = app_handler;
	bus->tx_fifo = fifo_new();
	bus->buf = buffer_new(500);

	reset_counters(bus);

	return bus;
}
