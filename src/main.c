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
#include "event.h"


#define MAX_EVENTS 10
#define BUF_SIZE 50000

config_t c;
config_t *config = &c;


int main(int argc, char * argv[]) {

	int epoll_fd, nfds, i;
	void * event_base;

	/* Unbuffered stdout */
	setbuf(stdout, NULL);
	
	event_base = event_base_new(MAX_EVENTS);
	

	/* Check user arguments and init config */
	if ( check_args(argc, argv, config) < 0 ) {
		exit(EXIT_FAILURE);
	}
	

	/* Setup state handler and init state  */
	if ( initial_transition(config, epoll_fd) < 0 ) {
		err_exit("No known operating mode selected\n");
	}

	/* Read incomming events on registered streams
	   and dispatch them to event handlers. Does 
	   not return. */
	event_base_dispatch(event_base);
	
	return 0;
}
