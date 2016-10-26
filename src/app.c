#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <signal.h>
#include <errno.h>

#include <Api/ApiCfg.h>
#include <Api/FpGeneral/ApiFpGeneral.h>
#include <Api/CodecList/ApiCodecList.h>
#include <Api/FpCc/ApiFpCc.h>
#include <Api/FpMm/ApiFpMm.h>
#include <Api/ProdTest/ApiProdTest.h>
#include <Api/RsStandard.h>
#include <Api/Linux/ApiLinux.h>
#include <termios.h>															// Must be included after natalie or collision occur

#include "app.h"
#include "dect.h"
#include "tty.h"
#include "error.h"
#include "boot.h"
#include "app.h"
#include "buffer.h"
#include "rawmail.h"
#include "busmail.h"
#include "eap.h"
#include "event.h"
#include "event_base.h"
#include "list.h"
#include "stream.h"
#include "handset.h"
#include "external_call.h"
#include "connection_init.h"
#include "api_parser.h"
#include "debugger.h"
#include "main.h"
#include "rawmailproxy.h"


//-------------------------------------------------------------
buffer_t * buf;
static int dect_fd;
static void *dect_bus;
void * dect_stream;
int epoll_fd;
struct sigaction act;
struct mail_protocol_t mailProto;




//-------------------------------------------------------------
static void sighandler(int signum, siginfo_t *info __attribute__((unused)), void *ptr __attribute__((unused))) {
	printf("Recieved signal %d\n", signum);
}



static void setup_signal_handler(void) {

	/* Setup signal handler. When writing data to a
	   client that closed the connection we get a 
	   SIGPIPE. We need to catch it to avoid being killed */
	memset(&act, 0, sizeof(act));
	act.sa_sigaction = sighandler;
	act.sa_flags = SA_SIGINFO;
	sigaction(SIGPIPE, &act, NULL);
}








//-------------------------------------------------------------
static void dect_handler(void *dect_stream __attribute__((unused)), void *event) {
	/* Add input to busmail subsystem */
	if (mailProto.receive(dect_bus, event) < 0) {
		printf("busmail buffer full\n");
	}
	
	/* Process whole packets in buffer. The previously registered
	   callback will be called for application frames */
	mailProto.dispatch(dect_bus);
}




//-------------------------------------------------------------
void app_init(void *base __attribute__((unused)), config_t * conf __attribute__ ((unused))) {
	
	int opts, res;
	char buf[16];

	printf("app_init\n");
	hwIsInternal = 0;

	// Probe for internal or external Dect
	if(access("/dev/dect", R_OK | W_OK) == 0 &&
			access("/dev/dectdbg", R_OK | W_OK) == 0) {
		printf("Trying internal Dect...\n");
		dect_fd = open("/dev/dect", O_RDWR);
		if(dect_fd > 0) {
			opts = fcntl(dect_fd, F_GETFL);
			fcntl(dect_fd, F_SETFL, opts | O_NONBLOCK);							// Set non-blocking mode
			// Internal dect returns an error if it doesn't exist
			res = read(dect_fd, buf, sizeof(buf));
			if(res == -1 && errno == ENXIO) {
				close(dect_fd);													// Close non-existing internal dect
			}
			else {
				fcntl(dect_fd, F_SETFL, opts);									// Restore blocking mode
				hwIsInternal = 1;
			}
		}
	}

	if(hwIsInternal) {
		/* CPU internal Dect. It has a simple
			* protocol, we always get entire packets
			* all at once. */
		mailProto.new = rawmail_new;
		mailProto.add_handler = rawmail_add_handler;
		mailProto.dispatch = rawmail_dispatch;
		mailProto.send = rawmail_send;
		mailProto.receive = rawmail_receive;
		mailProto.conf = rawmail_conf;
	}
	else if(access("/dev/ttyS1", R_OK | W_OK) == 0) {
		printf("Trying external Dect...\n");
		/* External Dect chip, connected via
		 * serial port. Packets arrive one byte
		 * at a time which we need to buffer. */
		dect_fd = tty_open("/dev/ttyS1");
		tty_set_raw(dect_fd);
		tty_set_baud(dect_fd, B115200);
		mailProto.new = busmail_new;
		mailProto.add_handler = busmail_add_handler;
		mailProto.dispatch = busmail_dispatch;
		mailProto.send = busmail_send;
		mailProto.receive = busmail_receive;
		mailProto.conf = busmail_conf;
	}

	if(!mailProto.new) exit_failure("No char dev for Dect com\n");

	/* Register dect stream */
	dect_stream = stream_new(dect_fd);
	stream_add_handler(dect_stream, MAX_EVENT_SIZE, dect_handler);
	event_base_add_stream(dect_stream);

	/* Init busmail subsystem */
	dect_bus = mailProto.new(dect_fd);

	/* Initialize submodules. The submodules will bind 
	   application frame handlers to the dect_bus */
	api_parser_init(dect_bus);
	debugger_init(dect_bus);
	connection_init(dect_bus);
	external_call_init(dect_bus);
	handset_init(dect_bus);
	rawmailproxy_init(dect_bus);

	setup_signal_handler();

	if(config.wait_dbg_client) return;

	if(hwIsInternal) {
		start_internal_dect();
	}
	else {
		// Reset external chip to normal operation
		printf("DECT TX TO BRCM RX\n");
		if(gpio_control(118, 0) == 0) {
			printf("RESET_DECT\n");
			dect_chip_reset();
		}
	}

	return;
}



