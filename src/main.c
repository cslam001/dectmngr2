#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <termios.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


#include "dect.h"
#include "tty.h"
#include "error.h"
#include "boot.h"
#include "state.h"
#include "util.h"
#include "app.h"
#include "nvs.h"
#include "test.h"
#include "list.h"
#include "busmail.h"
#include "stream.h"


#define MAX_EVENTS 10
#define BUF_SIZE 50000

config_t c;
config_t *config = &c;


int main(int argc, char * argv[]) {

	int epoll_fd, nfds, i;
	uint8_t inbuf[BUF_SIZE];
	uint8_t outbuf[BUF_SIZE];
	event_t event;
	event_t *e = &event;
	uint8_t buf[BUF_SIZE];
	void (*event_handler) (event_t *e);
	void *stream;
	struct epoll_event events[MAX_EVENTS];


	/* Unbuffered stdout */
	setbuf(stdout, NULL);
	

	/* Setup epoll instance */
	epoll_fd = epoll_create(10);
	if (epoll_fd == -1) {
		exit_failure("epoll_create\n");
	}


	/* Check user arguments and init config */
	if ( check_args(argc, argv, config) < 0 ) {
		exit(EXIT_FAILURE);
	}
	

	/* Setup state handler and init state  */
	if ( initial_transition(config, epoll_fd) < 0 ) {
		err_exit("No known operating mode selected\n");
	}

	for(;;) {

		nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
		if (nfds == -1) {
			exit_failure("epoll_wait\n");
		}

		for (i = 0; i < nfds; ++i) {
			if (events[i].data.ptr) {

				/* Get stream object */
				stream = events[i].data.ptr;

				/* Get list of stream handlers */
				event_handler = stream_get_handler(stream);

				/* Read data on fd */
				
				/* Dispatch event to all stream handlers */
				event_handler(stream);
			}
		}
	}
	
	return 0;
}
