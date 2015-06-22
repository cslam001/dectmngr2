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

#include <Api/FpGeneral/ApiFpGeneral.h>
#include <Api/CodecList/ApiCodecList.h>
#include <Api/FpCc/ApiFpCc.h>
#include <Api/FpMm/ApiFpMm.h>
#include <Api/ProdTest/ApiProdTest.h>
#include <Api/RsStandard.h>
#include <termios.h>

#include "dect.h"
#include "tty.h"
#include "error.h"
#include "state.h"
#include "boot.h"
#include "util.h"
#include "app.h"
#include "buffer.h"
#include "busmail.h"
#include "eap.h"

#include "internal_call.h"


#define INBUF_SIZE 5000
#define BUF_SIZE 50000

buffer_t * buf;
int client_connected;
void * dect_bus;
void * dect_stream, * listen_stream, * debug_stream, * proxy_stream;
void * client_stream;
int epoll_fd;
void * client_list;
void * client_bus;
void * client_list;
void * event_base;
struct sigaction act;


static void sighandler(int signum, siginfo_t * info, void * ptr) {

	printf("Recieved signal %d\n", signum);
}


static void eap(packet_t *p) {
	
	int i;

	printf("send to dect_bus\n");
	packet_dump(p);
	
	busmail_send0(dect_bus, &p->data[3], p->size - 3);
	
	/* /\* For RSX *\/ */
	/* busmail_send_prog(dect_bus, &p->data[3], p->size - 3, 0x81); */
}



static void client_packet_handler(packet_t *p) {

	busmail_t * m = (busmail_t *) &p->data[0];

	/* Production test command */
	if ( client_connected == 1 ) {

		/* Send packets to connected clients */
		printf("send to client_bus\n");
		packet_dump(p);
		eap_send(client_bus, &p->data[3], p->size - 3);
	}

}


static void client_handler(void * client_stream, void * event) {

	int client_fd = stream_get_fd(client_stream);

	/* Client connection */
	
	if ( event_count(event) == -1 ) {
					
		perror("recv");
	} else if ( event_count(event) == 0 ) {

		event_base_delete_stream(event_base, client_stream);
		
		/* Client connection closed */
		printf("client closed connection\n");
		if (close(client_fd) == -1) {
			exit_failure("close");
		}
					
		list_delete(client_list, client_fd);

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
	busmail_add_handler(dect_bus, client_packet_handler);
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
		stream_add_handler(client_stream, client_handler);

		/* Add client_stream to event dispatcher */
		event_base_add_stream(event_base, client_stream);

		/* Add client */
		list_add(client_list, client_fd);
		//list_each(client_list, list_connected);

		/* Setup client busmail connection */
		printf("setup client_bus\n");
		client_bus = eap_new(client_fd, eap);
		client_connected = 1;
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


void dect_handler(void * dect_stream, void * event) {


	/* Add input to busmail subsystem */
	if (busmail_write(dect_bus, event) < 0) {
		printf("busmail buffer full\n");
	}
	

	/* Process whole packets in buffer. The previously registered
	   callback will be called for application frames */
	busmail_dispatch(dect_bus);
}




void init_app_state(void * base, config_t * config) {
	
	int dect_fd, debug_fd, proxy_fd;

	printf("APP_STATE\n");
	event_base = base;

	/* Setup dect tty */
	dect_fd = tty_open("/dev/ttyS1");
	tty_set_raw(dect_fd);
	tty_set_baud(dect_fd, B115200);

	/* Register dect stream */
	dect_stream = stream_new(dect_fd);
	stream_add_handler(dect_stream, dect_handler);
	event_base_add_stream(event_base, dect_stream);

	/* Init busmail subsystem */
	dect_bus = busmail_new(dect_fd);

	/* Initialize submodules. The submodules will bind 
	   application frame handlers to the dect_bus */
	connection_init(dect_bus);
	api_parser_init(dect_bus);
	internal_call_init(dect_bus);

	/* Init client subsystem */
	client_init();

	/* Setup debug socket */
	debug_fd = setup_listener(10468, INADDR_ANY);
	debug_stream = stream_new(debug_fd);
	stream_add_handler(debug_stream, listen_handler);
	event_base_add_stream(event_base, debug_stream);

	/* Setup proxy socket */
	proxy_fd = setup_listener(7777, INADDR_LOOPBACK);
	proxy_stream = stream_new(proxy_fd);
	stream_add_handler(proxy_stream, listen_handler);
	event_base_add_stream(event_base, proxy_stream);

	/* Connect and reset dect chip */
	printf("DECT TX TO BRCM RX\n");
	if(gpio_control(118, 0)) return;
 	printf("RESET_DECT\n");
	if(dect_chip_reset()) return;

	return;
}


struct state_handler app_handler = {
	.state = APP_STATE,
	.init_state = init_app_state,
};

struct state_handler * app_state = &app_handler;
