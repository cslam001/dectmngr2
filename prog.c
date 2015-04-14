
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


#include "tty.h"
#include "error.h"
#include "state.h"
#include "boot.h"
#include "util.h"
#include "prog.h"
#include "buffer.h"
#include "packet.h"


#define INBUF_SIZE 5000

buffer_t * buf;








void init_prog_state(int dect_fd) {
	
	printf("PROG_STATE\n");

	tty_set_raw(dect_fd);
	tty_set_baud(dect_fd, B115200);

	/* Init input buffer */
	buf = buffer_new(50);
	
}


void handle_prog_package(event_t *e) {

	uint8_t header;
	packet_t packet;
	packet_t *p = &packet;
	p->fd = e->fd;

	util_dump(e->in, e->incount, "[READ]");

	/* Add input to buffer */
	if (buffer_add(buf, e->in, e->incount) < 0) {
		printf("buffer full\n");
	}
		
	//buffer_dump(buf);
	
	/* Process whole packets in buffer */
	packet_get(p, buf);

	packet_dump(p);
	
	packet_dispatch(p);
}





struct state_handler prog_handler = {
	.state = PROG_STATE,
	.init_state = init_prog_state,
	.event_handler = handle_prog_package,
};

struct state_handler * prog_state = &prog_handler;
