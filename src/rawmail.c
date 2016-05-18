/* rawmail
 * This is a simplification of busmaili.c, used only for
 * CPU internald Dect. Busmail on the other hand is used
 * for external Dect chip, where communication flows
 * through a serial port. */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>

#include "error.h"
#include "rawmail.h"
#include "busmail.h"
#include "buffer.h"
#include "list.h"
#include "event.h"



struct rawmail_connection_t {
	int fd;
	buffer_t * buf;
	void *application_handlers;
};



//-------------------------------------------------------------
// Send a rawmail to DECT stack API
int rawmail_send(void * _self, uint8_t *data, int size) {
	int len, writtenLen, chunkSize;
	struct rawmail_connection_t *bus;
	struct timespec req, rem;

	bus = (struct rawmail_connection_t *) _self;
	writtenLen = 0;
	util_dump(data, size, "[WRITE to intdect]");

	while(writtenLen < size) {
		chunkSize = size - writtenLen;

		len = write(bus->fd, data + writtenLen, chunkSize);
		if (len == -1 && errno != EINTR) {
			perror("Error writing to internal Dect");
			return -1;
		}
		
		writtenLen += len;
	}

	/* Workaround for kernel code that does not seem to handle
	 * multiple messages in the same read() call. This sleep
	 * introduces a 50ms delay so that the current message hopefully
	 * makes it into the kernel alone... */
	req.tv_sec = 0;
	req.tv_nsec = 50000000L;
	while(nanosleep(&req, &rem) < 0 && errno == EINTR) req = rem;

	return 0;
}



//-------------------------------------------------------------
// Internal Dect always gives us a whole package at
// a time so buffering is overkill. However, rawmail need
// to use same API as busmail.
int rawmail_receive(void *_self, void *event) {
	struct rawmail_connection_t *bus = (struct rawmail_connection_t *) _self;

	if (event_count(event) > 0 && buffer_write(bus->buf, event_data(event),
			event_count(event)) == 0 ) {
		return -1;
	}

	return 0;
}



//-------------------------------------------------------------
int rawmail_dispatch(void *_self) {
	struct rawmail_connection_t *bus;
	packet_t packet;
	busmail_t *mail;
	int len;

	bus = (struct rawmail_connection_t *) _self;
	if(!buffer_size(bus->buf)) return 0;

	mail = (busmail_t*) packet.data;
	mail->frame_header = 0;
	mail->program_id = 0;
	mail->task_id = API_TASK_ID;

	len = buffer_read(bus->buf, (uint8_t*) &mail->mail_header, 
		PACKET_SIZE - sizeof(busmail_t));
	if(len == -1) {
		perror("Error reading internal dect fd");
		return -1;
	}
	else if(len <= 0) {
		return 0;
	}

	packet.size = len + (uint8_t*) &mail->mail_header - (uint8_t*) packet.data;
	util_dump((uint8_t*) &mail->mail_header, len, "[READ from intdect]");
	list_call_each(bus->application_handlers, &packet);

	return 0;
}



//-------------------------------------------------------------
void rawmail_add_handler(void * _self, void (*app_handler)(packet_t *)) {
	struct rawmail_connection_t *bus;

	bus = (struct rawmail_connection_t *) _self;
	list_add(bus->application_handlers, app_handler);
}



//-------------------------------------------------------------
void* rawmail_new(int fd) {
	struct rawmail_connection_t *bus = (struct rawmail_connection_t *)
		calloc(1, sizeof(struct rawmail_connection_t));

	bus->fd = fd;
	bus->application_handlers = list_new();
	bus->buf = buffer_new(PACKET_SIZE);

	return bus;
}

