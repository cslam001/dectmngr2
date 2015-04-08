
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

#define BUSMAIL_PACKET_HEADER 0x10
#define BUSMAIL_PACKET_OVER_HEAD 4

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




void init_prog_state(int dect_fd) {
	
	printf("PROG_STATE\n");

	tty_set_raw(dect_fd);
	tty_set_baud(dect_fd, B115200);
}


void handle_prog_package(event_t *e) {

	util_dump(e->in, e->incount, "[READ]");

	if (inspect_rx(e) < 0) {
		printf("dropped packet\n");
	} 


	switch (e->in[0]) {

	default:
		printf("Unknown packet: %x\n", e->in[0]);

	}
	
}





struct state_handler prog_handler = {
	.state = PROG_STATE,
	.init_state = init_prog_state,
	.event_handler = handle_prog_package,
};

struct state_handler * prog_state = &prog_handler;
