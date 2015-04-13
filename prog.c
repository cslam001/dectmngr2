
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/epoll.h>
#include <termios.h>

#include "dect.h"
#include "tty.h"
#include "error.h"
#include "state.h"
#include "boot.h"
#include "util.h"
#include "prog.h"
#include "buffer.h"

#define BUSMAIL_PACKET_HEADER 0x10
#define BUSMAIL_PACKET_OVER_HEAD 4

#define PACKET_TYPE_MASK (1 << 7)
#define INFORMATION_FRAME (0 << 7)
#define CONTROL_FRAME (1 << 7)

#define CONTROL_FRAME_MASK ((1 << 7) | (1 << 6))
#define UNNUMBERED_CONTROL_FRAME ((1 << 7) | (1 << 6))
#define SUPERVISORY_CONTROL_FRAME ((1 << 7) | (0 << 6))

#define POLL_FINAL (1 << 3)
#define SAMB_POLL_SET 0xc8
#define SAMB_NO_POLL_SET 0xc0

#define INBUF_SIZE 5000

buffer_t * buf;

static int inspect_rx(event_t *e) {
	
	uint32_t data_size = 0, i;
	uint8_t crc = 0, crc_calc = 0;
	
	/* Check header */
	if (e->in[0] =! BUSMAIL_PACKET_HEADER) {
		printf("Drop packet: no header\n");
		return -1;
	}

	/* Check size */
	if (e->incount < BUSMAIL_PACKET_OVER_HEAD) {
		printf("Drop packet: packet size smaller then BUSMAIL_PACKET_OVER_HEAD %d < %d\n",
		       e->incount, BUSMAIL_PACKET_OVER_HEAD);
		return -1;
	}

	/* Do we have a full packet? */
	data_size = (((uint32_t) e->in[1] << 8) | e->in[2]);
	if (e->incount < (data_size + BUSMAIL_PACKET_OVER_HEAD)) {
		printf("Drop packet: not a full packet incount: %d < packet size: %d\n",
		       e->incount, data_size + BUSMAIL_PACKET_OVER_HEAD);
		return -1;
	}
	
	/* Read packet checksum */
	crc = (( (uint8_t) e->in[e->incount - 1]));

	/* Calculate checksum over data portion */
	for (i = 0; i < data_size; i++) {
		crc_calc += e->in[i + 3];
	}

	if (crc != crc_calc) {
		printf("Drop packet: bad packet checksum: %x != %x\n", crc, crc_calc);
		return -1;
	}

	return 0;
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
	  tx[3 + i] = data[i];
  }

  
  tx[3 + data_size] = crc;

  
  return tx;
}


static send_packet(void * data, int data_size, int fd) {

  int tx_size = data_size + BUSMAIL_PACKET_OVER_HEAD;
  uint8_t * tx = malloc(tx_size);
  
  make_tx_packet(tx, data, data_size);
  util_dump(tx, tx_size, "[WRITE]");
  write(fd, tx, tx_size);
  free(tx);
}



static void unnumbered_control_frame(event_t *e) {
	
	uint8_t header = e->in[3];
	uint8_t data;
	
	if (header == SAMB_POLL_SET) {
		printf("SAMB. Reset Rx and Tx counters\n");

		printf("Reply SAMB_NO_POLL_SET. \n");
		data = SAMB_NO_POLL_SET;
		send_packet(&data, 1, e->fd);
		
	} else {
		printf("Bad unnumbered control frame\n");
	}
}


void init_prog_state(int dect_fd) {
	
	printf("PROG_STATE\n");

	tty_set_raw(dect_fd);
	tty_set_baud(dect_fd, B115200);

	/* Init input buffer */
	buf = buf_new(INBUF_SIZE);
	
}


void handle_prog_package(event_t *e) {

	uint8_t header;

	util_dump(e->in, e->incount, "[READ]");

	/* Add input to buffer */
	
	
	/* Process whole packets in buffer */

	/* Drop invalid packets */
	if (inspect_rx(e) < 0) {
		printf("dropped packet\n");
	} 
	

	/* Route packet based on type */
	header = e->in[3];
	switch (header & PACKET_TYPE_MASK) {
		
	case INFORMATION_FRAME:
		printf("INFORMATION_FRAME\n");
		break;

	case CONTROL_FRAME:

		switch (header & CONTROL_FRAME_MASK) {
			
		case UNNUMBERED_CONTROL_FRAME:
			printf("UNNUMBERED_CONTROL_FRAME\n");
			unnumbered_control_frame(e);
			break;

		case SUPERVISORY_CONTROL_FRAME:
			printf("SUPERVISORY_CONTROL_FRAME\n");
			break;
		}

	break;

	default:
		printf("Unknown packet header: %x\n", header);

	}
	
}





struct state_handler prog_handler = {
	.state = PROG_STATE,
	.init_state = init_prog_state,
	.event_handler = handle_prog_package,
};

struct state_handler * prog_state = &prog_handler;
