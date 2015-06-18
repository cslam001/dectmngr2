#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <termios.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

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


/* Global variables */
config_t c;
config_t *config = &c;
struct sigaction act;
int client_connected = 0;
extern void * dect_bus;
struct epoll_event ev, events[MAX_EVENTS];
int epoll_fd;
void * client_list;


void sighandler(int signum, siginfo_t * info, void * ptr) {

	printf("Recieved signal %d\n", signum);
}


int main(int argc, char * argv[]) {
	
	int state = BOOT_STATE;
	int nfds, i, count, listen_fd, client_fd, ret;
	uint8_t inbuf[BUF_SIZE];
	uint8_t outbuf[BUF_SIZE];
	event_t event;
	event_t *e = &event;
	config_t c;
	config_t *config = &c;
	uint8_t buf[BUF_SIZE];
	void (*event_handler) (event_t *e);
	void *stream;


	/* Init client list */
	client_list = list_new();

	e->in = inbuf;
	e->out = outbuf;

	setbuf(stdout, NULL);
	
	/* Setup signal handler. When writing data to a
	   client that closed the connection we get a 
	   SIGPIPE. We need to catch it to avoid being killed */
	memset(&act, 0, sizeof(act));
	act.sa_sigaction = sighandler;
	act.sa_flags = SA_SIGINFO;
	sigaction(SIGPIPE, &act, NULL);
	

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

				/* Dispatch event to stream handler */
				stream = events[i].data.ptr;
				event_handler = stream_get_handler(stream);
				event_handler(stream);
			}
		}
	}
	
	return 0;
}
