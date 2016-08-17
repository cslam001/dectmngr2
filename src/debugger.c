
//
//
//                                ASCII art of the data flow between a debugger and Natalie Dect
// 
//              Debugger to Natalie
//  Internal
//  Natalie <------------------------------ Busmail <------------+-- EAP <------------------- buffer <--------- socket <-------- RSX debugger
//  /dev/dectdbg                       debug_int_bus             |  debugger_bus             debugger_stream   debuggerFd
//  debug_int_fd                                                 |  eap_rx_from_debugger()  rx_from_debugger()
//                                                               |  busmail_send_addressee()
//  External                                                     |
//  Natalie <------------------------------ Busmail <------------+
//   UART                                  dect_bus
//
//
//              Natalie to debugger
//
//  Natalie ----------> buffer -----------> Busmail ------------+--> EAP -------------------------------------> socket --------> RSX debugger
//  /dev/dectdbg   debug_int_stream      debug_int_bus          |  busmail_rx_from_natalie()                  debuggerFd
//  debug_int_fd   raw_rx_from_natalie()                        |  debugger_bus
//                                                              |  eap_send()
//  Natalie ----------> buffer -----------> Busmail ------------+
//  UART                                   dect_bus
//
//
//
// (C) Inteno 2015-2016
// Ronny Nilsson



#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "debugger.h"
#include "buffer.h"
#include "busmail.h"
#include "event.h"
#include "event_base.h"
#include "list.h"
#include "stream.h"
#include "eap.h"
#include "error.h"
#include "main.h"
#include "app.h"
#include "dect.h"


//-------------------------------------------------------------
static int listenFd;
static int debugger_connected;
static void *debugger_list;
static void *debugger_stream;
static eap_connection_t *debugger_bus;
static void *dbg_listen_stream;
static int debug_int_fd;
static void *debug_int_stream;
static busmail_connection_t *debug_int_bus;



//-------------------------------------------------------------
static void eap_rx_from_debugger(packet_t *p) {
	printf("send to dect_bus\n");
	busmail_send_addressee(debug_int_bus, p->data, p->size);
}



//-------------------------------------------------------------
// We receive data from RSX debugger socket
static void rx_from_debugger(void *debugger_stream, void *event) {
	int debuggerFd = stream_get_fd(debugger_stream);

	if ( event_count(event) == -1 ) {
		perror("recv");
	} else if ( event_count(event) == 0 ) {
		event_base_delete_stream(debugger_stream);
		
		/* Connection closed */
		printf("debugger closed connection\n");
		if (close(debuggerFd) == -1) {
			exit_failure("close");
		}
					
		list_delete(debugger_list, (void*) debuggerFd);

		/* TODO: Destroy client connection object here */
		//stream_delete()
		//eap_delete()
		//debugger_connected = 0;
	} else {
		util_dump(event_data(event), event_count(event), "[DBG RX]");
		eap_write(debugger_bus, event);

		/* Send packets from debugger to dect_bus */
		eap_dispatch(debugger_bus);
	}
}


//-------------------------------------------------------------
// Copy (sniff) all packets from Natalie to connected
// debuggers. Busmail has been parsed and here we get
// the payload.
static void busmail_rx_from_natalie(packet_t *p) {
	if (debugger_connected == 1) eap_send(debugger_bus, p->data, p->size);
}



//-------------------------------------------------------------
// Natalie always communicates debug data via Busmail. Internal
// Dect has a separate character device but external Dect
// multiplexes it together with Dect control commands. This
// function receives raw data from the *internal* char dev and
// copies it to the (internal+external) common Busmail protocol
// parser.
static void raw_rx_from_natalie(void *stream __attribute__ ((unused)), void *event) {
	if(!debugger_bus) return;

	if (busmail_receive(debugger_bus, event) < 0) {
		printf("busmail debug buffer full\n");
	}
	
	/* Process whole packets in buffer. The previously registered
	   callback will be called for application frames */
	busmail_dispatch(debugger_bus);
}



//-------------------------------------------------------------
// When RSX wants to connect, accept() it, create a socket
// file descriptor, wrap a buffer around the fd and attach
// a driver for the protocol RSX use.
static void dbg_listen_handler(void *dummy __attribute__ ((unused)), void *event __attribute__((unused))) {
	struct sockaddr_in peer_addr;
	socklen_t peer_addr_size;
	int debuggerFd;

	peer_addr_size = sizeof(peer_addr);

	debuggerFd = accept(stream_get_fd(dbg_listen_stream), 
			(struct sockaddr*) &peer_addr, &peer_addr_size);

	if(debuggerFd == -1) exit_failure("accept");

	printf("accepted connection: %d\n", debuggerFd);

	/* Setup stream object */
	debugger_stream = stream_new(debuggerFd);
	stream_add_handler(debugger_stream, MAX_EVENT_SIZE, rx_from_debugger);
	event_base_add_stream(debugger_stream);

	list_add(debugger_list, (void*) debuggerFd);

	/* Setup client busmail connection */
	debugger_bus = eap_new(debuggerFd, eap_rx_from_debugger);
	debugger_connected = 1;

	/* Reset external chip again so debugger can
	 * sniff that too. */
	if(!hwIsInternal && config.wait_dbg_client == 1) {
		dect_chip_reset();
		config.wait_dbg_client = 2;
	}
}



//-------------------------------------------------------------
// Open a listening TCP socket for RSX
int debugger_init(void *dect_bus) {	
	struct sockaddr_in my_addr;
	int opt = 1;
 
	memset(&my_addr, 0, sizeof(my_addr));
	my_addr.sin_family = AF_INET;
	my_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	my_addr.sin_port = htons(10468);
	
	listenFd = socket(AF_INET, SOCK_STREAM, 0);
	if(listenFd == -1 ) exit_failure("socket debugger");


	if(setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &(opt), sizeof(opt)) == -1) {
		exit_failure("setsockopt debugger");
	}

	if (bind(listenFd, (struct sockaddr*) &my_addr, sizeof(struct sockaddr)) == -1) {
		exit_failure("bind debugger");
	}
	
	if (listen(listenFd, MAX_LISTENERS) == -1) exit_failure("listen debugger");

	dbg_listen_stream = stream_new(listenFd);
	stream_add_handler(dbg_listen_stream, MAX_EVENT_SIZE, dbg_listen_handler);
	event_base_add_stream(dbg_listen_stream);

	/* For internal Dect the debug data is transfered via a separate
	 * character device. External Dect use same serial port as control
	 * data (multiplexed) */
	if(hwIsInternal) {
		debug_int_fd = open("/dev/dectdbg", O_RDWR);
		if(debug_int_fd == -1) {
			perror("Error opening internal dect debug");
			return -1;
		}

		// Add receive handler for internal raw debug data from Natalie.
		debug_int_stream = stream_new(debug_int_fd);
		stream_add_handler(debug_int_stream, MAX_EVENT_SIZE, raw_rx_from_natalie);
		event_base_add_stream(debug_int_stream);
		debug_int_bus = busmail_new(debug_int_fd);
	}
	else {
		/* For external Dect the debug data is transfered via same
		 * serial port as control data (multiplexed) and thus we
		 * don't need another receiver. */
		debug_int_bus = (busmail_connection_t*) dect_bus;
	}

	// Add Busmail protocol parser for debug data from Natalie.
	debugger_list = list_new();
	busmail_add_handler(debug_int_bus, busmail_rx_from_natalie);

	return 0;
}

