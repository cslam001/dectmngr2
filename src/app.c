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
#include <sys/socket.h>
#include <netinet/in.h>
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

#include "dect.h"
#include "tty.h"
#include "error.h"
#include "state.h"
#include "boot.h"
#include "util.h"
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

#define INBUF_SIZE 5000
#define BUF_SIZE 50000

//-------------------------------------------------------------
buffer_t * buf;
int client_connected;
static int dect_fd;
static void *dect_bus;
void * dect_stream, * listen_stream, * debug_srv_steam;
void * client_stream;
static void *debug_bus;
static void *debug_int_stream;
static int debug_int_fd;
int epoll_fd;
void * client_list;
void * client_bus;
struct sigaction act;
struct mail_protocol_t mailProto;
int hwIsInternal;
static config_t *conf;


//-------------------------------------------------------------
static int dect_hw_reset(void);


//-------------------------------------------------------------
static void sighandler(int signum, siginfo_t * info, void * ptr) {
	printf("Recieved signal %d\n", signum);
}


static void eap(packet_t *p) {
	printf("send to dect_bus\n");
	busmail_send_addressee(debug_bus, p->data, p->size);
}



static void client_packet_handler(packet_t *p) {
	// Send (sniff) all packets to connected clients
	if (client_connected == 1) eap_send(client_bus, p->data, p->size);
}


static void client_handler(void * client_stream, void * event) {

	int client_fd = stream_get_fd(client_stream);

	/* Client connection */
	
	if ( event_count(event) == -1 ) {
					
		perror("recv");
	} else if ( event_count(event) == 0 ) {

		event_base_delete_stream(client_stream);
		
		/* Client connection closed */
		printf("client closed connection\n");
		if (close(client_fd) == -1) {
			exit_failure("close");
		}
					
		list_delete(client_list, (void*) client_fd);

		/* Destroy client connection object here */

	} else {

		/* Data is read from client */
		util_dump(event_data(event), event_count(event), "[CLIENT]");

		/* Send packets from clients to dect_bus */
		eap_write(client_bus, event);
		eap_dispatch(client_bus);
	}
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


static void client_init(void) {
	
	setup_signal_handler();
	client_list = list_new();
	busmail_add_handler(debug_bus, client_packet_handler);
}

static void listen_handler(void * listen_stream, void * event) {

	int client_fd;
	void * client_stream;
	struct sockaddr_in my_addr, peer_addr;
	socklen_t peer_addr_size;


	peer_addr_size = sizeof(peer_addr);

	if ( (client_fd = accept(stream_get_fd(listen_stream), (struct sockaddr *) &peer_addr, &peer_addr_size)) == -1) {
		exit_failure("accept");
	} else {

		printf("accepted connection: %d\n", client_fd);

		/* Setup stream object */
		client_stream = stream_new(client_fd);
		stream_add_handler(client_stream, MAX_EVENT_SIZE, client_handler);

		/* Add client_stream to event dispatcher */
		event_base_add_stream(client_stream);

		/* Add client */
		list_add(client_list, (void*) client_fd);
		//list_each(client_list, list_connected);

		/* Setup client busmail connection */
		printf("setup client_bus on fd %d\n", client_fd);
		client_bus = eap_new(client_fd, eap);
		client_connected = 1;

		if(conf->wait_dbg_client == 1) {
			dect_hw_reset();
			conf->wait_dbg_client = 2;
		}
	}
}



static int setup_listener(uint16_t port, uint32_t type) {

	int listen_fd, opt = 1;
	struct sockaddr_in my_addr, peer_addr;
	socklen_t peer_addr_size;
 
	memset(&my_addr, 0, sizeof(my_addr));
	my_addr.sin_family = AF_INET;
	my_addr.sin_addr.s_addr = htonl(type);
	my_addr.sin_port = htons(port);
	
	if ( (listen_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1 ) {
		exit_failure("socket");
	}

	if ( (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &(opt), sizeof(opt))) == -1 ) {
		exit_failure("setsockopt");
	}

	if ( (bind(listen_fd, (struct sockaddr *) &my_addr, sizeof(struct sockaddr))) == -1) {
		exit_failure("bind");
	}
	
	if ( (listen(listen_fd, MAX_LISTENERS)) == -1 ) {
		exit_failure("bind");
	}


	return listen_fd;
}



//-------------------------------------------------------------
static void dect_handler(void * dect_stream, void * event) {
	/* Add input to busmail subsystem */
	if (mailProto.receive(dect_bus, event) < 0) {
		printf("busmail buffer full\n");
	}
	
	/* Process whole packets in buffer. The previously registered
	   callback will be called for application frames */
	mailProto.dispatch(dect_bus);
}



//-------------------------------------------------------------
static void debug_handler(void *stream, void *event) {
	/* Add input to busmail subsystem */
	if (busmail_receive(debug_bus, event) < 0) {
		printf("busmail debug buffer full\n");
	}
	
	/* Process whole packets in buffer. The previously registered
	   callback will be called for application frames */
	busmail_dispatch(debug_bus);
}



//-------------------------------------------------------------
void app_init(void * base, config_t * config) {
	
	int debug_sock;

	printf("app_init\n");
	conf = config;
	
	if(access("/dev/dect", R_OK | W_OK) == 0 &&
			(dect_fd = open("/dev/dect", O_RDWR)) &&
			(debug_int_fd = open("/dev/dectdbg", O_RDWR))) {
		/* CPU internal Dect. It has a simple
		 * protocol, we always get entire packets
		 * all at once. */
		hwIsInternal = 1;
		mailProto.new = rawmail_new;
		mailProto.add_handler = rawmail_add_handler;
		mailProto.dispatch = rawmail_dispatch;
		mailProto.send = rawmail_send;
		mailProto.receive = rawmail_receive;
	}
	else if(access("/dev/ttyS1", R_OK | W_OK) == 0) {
		/* External Dect chip, connected via
		 * serial port. Packets arrive one byte
		 * at a time which we need to buffer. */
		hwIsInternal = 0;
		dect_fd = tty_open("/dev/ttyS1");
		tty_set_raw(dect_fd);
		tty_set_baud(dect_fd, B115200);
		mailProto.new = busmail_new;
		mailProto.add_handler = busmail_add_handler;
		mailProto.dispatch = busmail_dispatch;
		mailProto.send = busmail_send;
		mailProto.receive = busmail_receive;
	}
	else {
		exit_failure("No char dev for Dect com\n");
	}

	/* Register dect stream */
	dect_stream = stream_new(dect_fd);
	stream_add_handler(dect_stream, MAX_EVENT_SIZE, dect_handler);
	event_base_add_stream(dect_stream);

	/* Init busmail subsystem */
	dect_bus = mailProto.new(dect_fd);

	/* Initialize submodules. The submodules will bind 
	   application frame handlers to the dect_bus */
	api_parser_init(dect_bus);
	connection_init(dect_bus);
	external_call_init(dect_bus);
	handset_init(dect_bus);

	/* Setup debug socket */
	debug_sock = setup_listener(10468, INADDR_ANY);
	debug_srv_steam = stream_new(debug_sock);
	stream_add_handler(debug_srv_steam, MAX_EVENT_SIZE, listen_handler);
	event_base_add_stream(debug_srv_steam);

	/* For internal Dect the debug data is transfered
	 * via a separatde character device. External Dect
	 * use same serial port as control data (multiplexed) */
	if(hwIsInternal) {
		debug_int_stream = stream_new(debug_int_fd);
		stream_add_handler(debug_int_stream, MAX_EVENT_SIZE, debug_handler);
		event_base_add_stream(debug_int_stream);
		debug_bus = busmail_new(debug_int_fd);
	}
	else {
		/* For external Dect the debug data is transfered
		 * via same serial port as control data (multiplexed). */
		debug_bus = dect_bus;
	}

	/* Init debug client subsystem */
	client_init();

	if(conf->wait_dbg_client) return;

	dect_hw_reset();

	return;
}



//-------------------------------------------------------------
static int dect_hw_reset(void) {
	if(hwIsInternal) {
		return start_internal_dect();
	}
	else {
		// Reset chip
		printf("DECT TX TO BRCM RX\n");
		if(gpio_control(118, 0)) return -1;
		printf("RESET_DECT\n");
		if(dect_chip_reset()) return -1;
	}

	return 0;
}
