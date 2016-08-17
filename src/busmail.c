#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>

#include "error.h"
#include "busmail.h"
#include "fifo.h"
#include "list.h"
#include "event.h"
#include "tty.h"




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

  
  tx[3 + data_size] = crc;

  
  return tx;
}



static void send_packet(void * data, int data_size, int fd) {

  int tx_size = data_size + BUSMAIL_PACKET_OVER_HEAD;
  int len, writtenLen, chunkSize;
  uint8_t * tx = malloc(tx_size);

  writtenLen = 0;
  make_tx_packet(tx, data, data_size);
  util_dump(tx, tx_size, "[WRITE to dect]");

  while(writtenLen < tx_size) {
    chunkSize = tx_size - writtenLen;

    len = write(fd, tx + writtenLen, chunkSize);
    if (len == -1 && errno != EINTR) {
      perror("Error writing to external Dect");
      break;
    }
		
    writtenLen += len;
  }

  if(tty_drain(fd)) perror("Error draining to external Dect");

  free(tx);
}




static void unnumbered_control_frame(void * _self, packet_t *p) {
	
	busmail_connection_t * bus = (busmail_connection_t *) _self;

	uint8_t header = p->data[0];
	uint8_t data;
	
	if (header == SAMB_POLL_SET) {
		printf("SAMB. Reset Rx and Tx counters\n");
		reset_counters(bus);

		printf("Reply SAMB_NO_POLL_SET. \n");
		data = SAMB_NO_POLL_SET;
		send_packet(&data, 1, bus->fd);
		
	} else {
		printf("Bad unnumbered control frame\n");
	}
}


static int packet_inspect(packet_t *p) {
	
	uint32_t data_size = 0, i;
	uint8_t crc = 0, crc_calc = 0;
	
	/* Check header */
	if (p->data[0] != BUSMAIL_PACKET_HEADER) {
		printf("Drop packet: no header\n");
		return -1;
	}

	/* Check size */
	if (p->size < BUSMAIL_PACKET_OVER_HEAD) {
		printf("Drop packet: packet size smaller then BUSMAIL_PACKET_OVER_HEAD %d < %d\n",
		       p->size, BUSMAIL_PACKET_OVER_HEAD);
		return -1;
	}

	/* Do we have a full packet? */
	data_size = (((uint32_t) p->data[1] << 8) | p->data[2]);
	if (p->size < (data_size + BUSMAIL_PACKET_OVER_HEAD)) {
		printf("Drop packet: not a full packet incount: %d < packet size: %d\n",
		       p->size, data_size + BUSMAIL_PACKET_OVER_HEAD);
		return -1;
	}
	
	/* Read packet checksum */
	crc = (( (uint8_t) p->data[p->size - 1]));

	/* Calculate checksum over data portion */
	for (i = 0; i < data_size; i++) {
		crc_calc += p->data[i + 3];
	}

	if (crc != crc_calc) {
		printf("Drop packet: bad packet checksum: %x != %x\n", crc, crc_calc);
		return -1;
	}

	return 0;
}




static uint8_t make_supervisory_frame(void * _self, uint8_t suid, uint8_t pf) {
	
	busmail_connection_t * bus = (busmail_connection_t *) _self;
	uint8_t header;

	header = SUPERVISORY_CONTROL_FRAME | (suid << SUID_OFFSET) | 
		(pf << PF_OFFSET) | (bus->rx_seq_l);

	return header;
}


static uint8_t make_info_frame(void * _self, uint8_t pf) {
	
	busmail_connection_t * bus = (busmail_connection_t *) _self;
	uint8_t header;

	header = ( (bus->tx_seq_l << TX_SEQ_OFFSET) | (pf << PF_OFFSET) | bus->rx_seq_l );

	return header;
}



static void busmail_tx(void * _self, uint8_t * data, int size, uint8_t pf, uint8_t task_id, uint8_t prog_id) {

	busmail_connection_t * bus = (busmail_connection_t *) _self;
	uint8_t tx_seq_tmp, rx_seq_tmp;
	busmail_t * r;	
	
	r = malloc(BUSMAIL_PACKET_OVER_HEAD - 1 + size);
	if (!r) {
		exit_failure("malloc");
	}

		
	r->frame_header = make_info_frame(bus, pf);
	r->program_id = prog_id;
	r->task_id = task_id;
	memcpy(&(r->mail_header), data, size);
	
	send_packet(r, BUSMAIL_PACKET_OVER_HEAD - 1 + size, bus->fd);
	free(r);
	
	/* For every packet we transmitt we increase
	 * a sequence number. */
	bus->tx_seq_l++;
	bus->tx_seq_l &= 7u;							// Wrap to 0 when reaching 8
}


// Send a busmail to DECT stack API
int busmail_send(void * _self, uint8_t * data, int size) {

	busmail_connection_t * bus = (busmail_connection_t *) _self;
	tx_packet_t * tx = calloc(sizeof(tx_packet_t), 1);
	
	tx->data = malloc(size);
	memcpy(tx->data, data, size);
	tx->task_id = API_TASK_ID;
	tx->size = size;
	
	//util_dump(tx->data, tx->size, "fifo_add");
	//fifo_add(tx_fifo, tx);
       
	busmail_tx(bus, tx->data, tx->size, PF, tx->task_id, API_PROG_ID);
	free(tx);

	return 0;
}

// Send a busmail to DECT stack API
// Allow user to set task id in outgoing frame
void busmail_send_task(void * _self, uint8_t * data, int size, int task_id) {

	busmail_connection_t * bus = (busmail_connection_t *) _self;
	tx_packet_t * tx = calloc(sizeof(tx_packet_t), 1);
	
	tx->data = malloc(size);
	memcpy(tx->data, data, size);
	tx->task_id = task_id;
	tx->size = size;
	
	//util_dump(tx->data, tx->size, "fifo_add");
	//fifo_add(tx_fifo, tx);
       
	busmail_tx(bus, tx->data, tx->size, PF, tx->task_id, API_PROG_ID);
	free(tx);
}


/* Send a busmail where the addressee has
 * already been specified by previously
 * received packet. */
void busmail_send_addressee(void * _self, uint8_t * data, int size) {

	busmail_connection_t * bus = (busmail_connection_t *) _self;
	tx_packet_t * tx = calloc(sizeof(tx_packet_t), 1);
	
	tx->data = malloc(size - BUSMAIL_HEADER_SIZE);
	memcpy(tx->data, data + BUSMAIL_HEADER_SIZE, size - BUSMAIL_HEADER_SIZE);
	tx->task_id = data[TASK_ID_OFFSET];;
	tx->size = size - BUSMAIL_HEADER_SIZE;

	busmail_tx(bus, tx->data, tx->size, PF, tx->task_id, data[PROG_ID_OFFSET]);
	free(tx);
}


void busmail_ack(void * _self) {

	busmail_connection_t * bus = (busmail_connection_t *) _self;
	uint8_t sh, rx_seq_tmp;

	sh = make_supervisory_frame(bus, SUID_RR, NO_PF);
	rx_seq_tmp = (sh & RX_SEQ_MASK) >> RX_SEQ_OFFSET;
	
	printf("WRITE: BUSMAIL_ACK %d\n", rx_seq_tmp);
	send_packet(&sh, 1, bus->fd);
}


static void supervisory_control_frame(void * _self, packet_t *p) {
	
	busmail_connection_t * bus = (busmail_connection_t *) _self;
	busmail_t * m = (busmail_t *) &p->data[0];
	uint8_t pf, suid;

	bus->rx_seq_r = (m->frame_header & RX_SEQ_MASK) >> RX_SEQ_OFFSET;
	pf = (m->frame_header & PF_MASK) >> PF_OFFSET;
	suid = (m->frame_header & SUID_MASK) >> SUID_OFFSET;

	//packet_dump(p);
	
	switch (suid) {
		
	case SUID_RR:
		printf("SUID_RR\n");
		break;

	case SUID_REJ:
		printf("SUID_REJ\n");
		break;

	case SUID_RNR:
		printf("SUID_RNR\n");
		break;
	}

	printf("rx_seq_r: %d\n", bus->rx_seq_r);
	printf("pf: %d\n", pf);
}


static void information_frame(void * _self, packet_t *p) {

	busmail_connection_t * bus = (busmail_connection_t *) _self;
	busmail_t * m = (busmail_t *) &p->data[0];
	uint8_t pf, sh, ih;
	tx_packet_t * tx;
	int ack = true;


	/* Update busmail packet counters */
	bus->tx_seq_r = (m->frame_header & TX_SEQ_MASK) >> TX_SEQ_OFFSET;
	bus->rx_seq_r = (m->frame_header & RX_SEQ_MASK) >> RX_SEQ_OFFSET;
	pf = (m->frame_header & PF_MASK) >> PF_OFFSET;

	util_dump(p->data, p->size, "[DECT]");

	/* Set next packet receive sequence number to
	 * whatever remote want. We don't implement
	 * resending (yet) and thus the numbers are
	 * of minor importance. */
	bus->rx_seq_l = bus->tx_seq_r + 1;
	bus->rx_seq_l &= 7u;							// Wrap to 0 at 8

	/* Always ack with control frame */
	busmail_ack(bus);

	/* Process application frame. The application frame callback will enqueue 
	   outgoing packets on tx_fifo and directly transmit packages with busmail_send() */
	list_call_each(bus->application_handlers, p);
}



int busmail_receive(void *_self, void *event) {

	busmail_connection_t * bus = (busmail_connection_t *) _self;
	
	if (event_count(event) > 0) {
		if ( buffer_write(bus->buf, event_data(event), event_count(event)) == 0 ) {
			return -1;
		}
	}
	
	return 0;
}


int busmail_get(void * _self, packet_t *p) {

	busmail_connection_t * bus = (busmail_connection_t *) _self;
	int i, stop, size, read = 0;
	uint8_t crc = 0, crc_calc = 0;
	uint8_t buf[5000];


	/* Do we have a start of frame? */
	while (buffer_read(bus->buf, buf, 1) > 0) {
		read++;
		if (buf[0] == BUSMAIL_PACKET_HEADER) {
			break;
		}
	}

	/* Return if we did not read any data */
	if (read == 0) {
		return -1;
	}

	/* Do we have a full header? */
	if (buffer_size(bus->buf) < 2) {
		buffer_rewind(bus->buf, 1);
		return -1;
	}
	buffer_read(bus->buf, buf + 1, 2);

	/* Packet size */
	size = (((uint32_t) buf[1] << 8) | buf[2]);
	
	/* Do we have a full packet? */
	/* Data + crc */
	if (1 + size > buffer_size(bus->buf)) {
		buffer_rewind(bus->buf, 3);
		return -1;
	}
	buffer_read(bus->buf, buf + 3, size + 1);
	
	/* Read packet checksum */
	crc = (( (uint8_t) buf[BUSMAIL_PACKET_OVER_HEAD + size - 1]));

	/* Calculate checksum over data portion */
	for (i = 0; i < size; i++) {
		crc_calc += buf[i + 3];
	}

	if (crc != crc_calc) {
		printf("Drop packet: bad packet checksum: %x != %x\n", crc, crc_calc);
		return -1;
	}

	/* Copy data portion to packet */
	memcpy(p->data, buf + 3, size);
	p->size = size;

	util_dump(buf, size + BUSMAIL_PACKET_OVER_HEAD, "[PACKET]");
	return 0;
}


void packet_dump(packet_t *p) {
	
	unsigned int i;

	printf("\n[PACKET %d] - ", p->size);
	for (i = 0; i < p->size; i++) {
		printf("%02x ", p->data[i]);
	}
	printf("\n");
}


int busmail_dispatch(void * _self) {

	busmail_connection_t * bus = (busmail_connection_t *) _self;
	packet_t packet;
	packet_t *p = &packet;
	busmail_t * m;

	/* Process whole packets in buffer */
	while ( busmail_get(bus, p) == 0) {

		m = (busmail_t *) &p->data[0];

		/* Route packet based on type */
		switch (m->frame_header & PACKET_TYPE_MASK) {
		
		case INFORMATION_FRAME:
			information_frame(bus, p);
			break;

		case CONTROL_FRAME:

			switch (m->frame_header & CONTROL_FRAME_MASK) {
			
			case UNNUMBERED_CONTROL_FRAME:
				printf("UNNUMBERED_CONTROL_FRAME\n");
				unnumbered_control_frame(bus, p);
				break;

			case SUPERVISORY_CONTROL_FRAME:
				supervisory_control_frame(bus, p);
				break;
			}

			break;

		default:
			printf("Unknown packet header: %x\n", m->frame_header);

		}
	}

	return 0;
}


void busmail_add_handler(void * _self , void (*app_handler)(packet_t *)) {

	busmail_connection_t * bus = (busmail_connection_t *) _self;

	list_add(bus->application_handlers, app_handler);
}


void * busmail_new(int fd) {

	busmail_connection_t * bus = (busmail_connection_t *) calloc(sizeof(busmail_connection_t), 1);

	bus->fd = fd;
	bus->application_handlers = list_new();
	bus->tx_fifo = fifo_new();
	bus->buf = buffer_new(500);

	return bus;
}
