
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
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


void prog_init(void * event_base, config_t * config) {
	
	void * stream;
	int dect_fd;

	printf("prog_init\n");

	/* Setup dect tty */
	dect_fd = tty_open("/dev/ttyS1");
	tty_set_raw(dect_fd);
	tty_set_baud(dect_fd, B19200);
	
	/* Register dect stream with event base */
	stream = (void *) stream_new(dect_fd);
	event_base_add_stream(event_base, stream);

	/* Init boot state */
	boot_init(stream);
}



