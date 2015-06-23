#include <sys/epoll.h>
#include <stdlib.h>


#include "event_base.h"

#define MAX_EVENTS 10


/* Module scope variables */
static int epoll_fd;

void * event_base_new(int count) {
	
	/* Setup epoll instance */
	epoll_fd = epoll_create(count);
	if (epoll_fd == -1) {
		exit_failure("epoll_create\n");
	}
}


void * event_base_add_stream(void * _self, void * stream) {

	struct epoll_event ev;

	ev.events = EPOLLIN;
	ev.data.ptr = stream;

	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, stream_get_fd(stream), &ev) == -1) {
		exit_failure("epoll_ctl\n");
	}
	

}

void * event_base_delete_stream(void * _self, void * stream) {
	
	/* Deregister fd */
	if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, stream_get_fd(stream), NULL) == -1) {
		exit_failure("epoll_ctl\n");
	}
}


void * event_base_dispatch(void * _self) {

	int nfds, i;
	struct epoll_event events[MAX_EVENTS];
	void (*stream_handler) (void * stream, void * event);
	void * stream, * event;
	

	for(;;) {

		nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
		if (nfds == -1) {
			exit_failure("epoll_wait\n");
		}

		for (i = 0; i < nfds; ++i) {
			if (events[i].data.ptr) {

				/* Get stream object */
				stream = events[i].data.ptr;

				/* Get stream handler */
				stream_handler = stream_get_handler(stream);

				/* Read data on fd */
				event = event_new(stream);

				/* Dispatch event to stream handler */
				stream_handler(stream, event);

				event_destroy(event);
			}
		}
	}


}
