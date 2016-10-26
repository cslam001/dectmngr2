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
	int64_t prevTxTimestamp;													// Timestamp of last transmitt
	int64_t minPktDelay;														// Minimum delay in usec between transmitted packets
};



//-------------------------------------------------------------
// Send a rawmail to DECT stack API
int rawmail_send(void * _self, uint8_t *data, int size) {
	int len, writtenLen, chunkSize;
	struct rawmail_connection_t *bus;
	int64_t now, timeSincePrevTx;
	struct timespec req, rem;
	char str[32];

	bus = (struct rawmail_connection_t *) _self;
	writtenLen = 0;
	snprintf(str, sizeof(str), "[Rawmail write %d]",  bus->fd);
	util_dump(data, size, str);


	/* Workaround for kernel code that does not seem to handle
	 * multiple messages in the same read() call. This sleep
	 * introduces a delay so that the current message hopefully
	 * makes it into the kernel alone... */
	now = timeSinceStart();
	timeSincePrevTx = now - bus->prevTxTimestamp;								// Number of usec since last transmitt
	if(timeSincePrevTx < bus->minPktDelay) {									// Only sleep if previous message was sent less than xx usec ago
		req.tv_sec = 0;
		req.tv_nsec = (long) (bus->minPktDelay - timeSincePrevTx) * 1000L;		// Convert to nano sec
		while(nanosleep(&req, &rem) < 0 && errno == EINTR) req = rem;
	}

	// Send all binary data
	while(writtenLen < size) {
		chunkSize = size - writtenLen;

		len = write(bus->fd, data + writtenLen, chunkSize);
		if (len == -1 && errno != EINTR) {
			perror("Error writing to internal Dect");
			return -1;
		}
		
		writtenLen += len;
	}

	// Timestamp of when transmission finished
	bus->prevTxTimestamp = timeSinceStart();

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
	char str[32];
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
	snprintf(str, sizeof(str), "[Rawmail read %d]",  bus->fd);
	util_dump((uint8_t*) &mail->mail_header, len, str);
	list_call_each(bus->application_handlers, &packet);

	return 0;
}



//-------------------------------------------------------------
// Setup configuration parameters for each particular connection
int rawmail_conf(void *_self, enum busmail_conf_t key, void *value) {
	struct rawmail_connection_t *bus;

	bus = (struct rawmail_connection_t *) _self;

	switch(key) {
		case MIN_PKT_DELAY:
			if(*((int64_t*) value) < 0LL) return -1;
			bus->minPktDelay = *((int64_t*) value);
			break;

		default:
			return -1;
	}

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
	printf("New rawmail buffer for fd %d\n", fd);

	return bus;
}

