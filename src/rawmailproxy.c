
//
//
//                         ASCII art of the data flow between a third party application and Natalie Dect
// 
//             Third party app to Natalie
//  Natalie <------------------------------ Busmail <--------------- Rawmail <---------------- buffer <--------- socket <-------- third party app
//  /dev/dect                          proxy_int_bus                proxy_bus                  proxy_stream     proxyFd
//                                     busmail_send()               rawmail_rx_from_client()   rx_from_client()
//
//
//
//              Natalie to debugger
//
//  Natalie ----------> buffer -----------> Busmail -----------------> Rawmail ------------------------> socket --------> third party app
//  /dev/dect                               proxy_int_bus              proxy_bus                         proxyFd
//                                          busmail_rx_from_natalie()  rawmail_send()
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
#include <netinet/tcp.h>

#include "rawmailproxy.h"
#include "buffer.h"
#include "rawmail.h"
#include "event.h"
#include "event_base.h"
#include "list.h"
#include "stream.h"
#include "eap.h"
#include "error.h"
#include "main.h"
#include "app.h"
#include "dect.h"
#include "connection_init.h"


//-------------------------------------------------------------
static int listenFd;
static int client_connected;
static void *proxy_list;
static void *proxy_stream;
static eap_connection_t *proxy_bus;
static void *proxy_listen_stream;
static busmail_connection_t *proxy_int_bus;



//-------------------------------------------------------------
// We receive rawmail data from third party app
static void rawmail_rx_from_client(packet_t *p) {
	uint8_t *data;
	uint32_t len;

	data = (uint8_t*) p->data + BUSMAIL_HEADER_SIZE;
	len = p->size - BUSMAIL_HEADER_SIZE;

	//printf("proxy to dect_bus len %d\n", len);
	mailProto.send(proxy_int_bus, data, len);
}



//-------------------------------------------------------------
// We receive data from third party app socket
static void rx_from_client(void *proxy_stream, void *event) {
	int proxyFd = stream_get_fd(proxy_stream);

	if(event_count(event) == -1) {
		perror("recv");
	}
	else if(event_count(event) == 0) {
		event_base_delete_stream(proxy_stream);
		
		/* Connection closed */
		printf("proxy app closed connection\n");
		if(close(proxyFd) == -1) exit_failure("close");
					
		list_delete(proxy_list, (void*) proxyFd);

		/* TODO: Destroy client connection object here */
		//stream_delete()
		//eap_delete()
		//debugger_connected = 0;
		client_connected = 0;
	}
	else if(connection.hasInitialized && connection.radio == ACTIVE) {
		util_dump(event_data(event), event_count(event), "[Proxy read]");
		rawmail_receive(proxy_bus, event);

		/* Send packets from debugger to dect_bus */
		rawmail_dispatch(proxy_bus);
	}
}


//-------------------------------------------------------------
// Copy (sniff) all packets from Natalie to connected
// third party clients. Rawmail has been parsed and here
// we get the payload.
static void rawmail_rx_from_natalie(packet_t *p) {
	struct proxy_packet proxyPacket;

	if(!client_connected) return;

	/* Create a binary proxy packet and send
	 * it to the third party app. */
	proxyPacket.size = p->size - BUSMAIL_HEADER_SIZE +
		sizeof(struct proxy_packet) - MAX_MAIL_SIZE;
	proxyPacket.type = DECT_PACKET;
	memcpy(proxyPacket.data, p->data + BUSMAIL_HEADER_SIZE,
		p->size - BUSMAIL_HEADER_SIZE);

	//printf("proxy to third party app len %d\n", proxyPacket.size);
	rawmail_send(proxy_bus, (uint8_t*) &proxyPacket, proxyPacket.size);
}



//-------------------------------------------------------------
// When third party app wants to connect, accept() it, create
// a socket file descriptor, wrap a buffer around the fd and
// attach a driver for the protocol third party app use.
static void proxy_listen_handler(void *dummy __attribute__ ((unused)), void *event __attribute__((unused))) {
	struct sockaddr_in peer_addr;
	socklen_t peer_addr_size;
	int proxyFd;

	peer_addr_size = sizeof(peer_addr);

	proxyFd = accept(stream_get_fd(proxy_listen_stream), 
			(struct sockaddr*) &peer_addr, &peer_addr_size);

	if(proxyFd == -1) exit_failure("accept");

	printf("Rawmail proxy accepted connection: %d\n", proxyFd);

	/* Setup stream object */
	proxy_stream = stream_new(proxyFd);
	stream_add_handler(proxy_stream, MAX_EVENT_SIZE, rx_from_client);
	event_base_add_stream(proxy_stream);

	list_add(proxy_list, (void*) proxyFd);

	/* Setup client rawmail connection */
	proxy_bus = rawmail_new(proxyFd);
	rawmail_add_handler(proxy_bus, rawmail_rx_from_client);
	client_connected = 1;
	perhaps_disable_radio();
}



//-------------------------------------------------------------
// Return true if we have an active connected third
// party rawmail application.
int hasProxyClient(void) {
	return (client_connected == 1);
}



//-------------------------------------------------------------
// Open a listening TCP socket for third party rawmail applications
int rawmailproxy_init(void *dect_bus) {	
	struct sockaddr_in my_addr;
	int opt;
 
	memset(&my_addr, 0, sizeof(my_addr));
	my_addr.sin_family = AF_INET;
	my_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	my_addr.sin_port = htons(7777);
	
	listenFd = socket(AF_INET, SOCK_STREAM, 0);
	if(listenFd == -1 ) exit_failure("rawmail proxy");

	opt = 1;
	if(setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
		exit_failure("setsockopt rawmail proxy reuse addr");
	}

	opt = 1;
	if(setsockopt(listenFd, SOL_SOCKET, SO_RCVLOWAT, &opt, sizeof(opt)) == -1) {
		exit_failure("setsockopt rawmail proxy receive low at");
	}

	opt = 1;
	if(setsockopt(listenFd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) == -1) {
		exit_failure("setsockopt rawmail proxy disable Nagles algorithm");
	}

	if(bind(listenFd, (struct sockaddr*) &my_addr, sizeof(struct sockaddr)) == -1) {
		exit_failure("bind rawmail proxy");
	}
	
	if (listen(listenFd, MAX_LISTENERS) == -1) exit_failure("listen rawmail proxy");

	proxy_listen_stream = stream_new(listenFd);
	stream_add_handler(proxy_listen_stream, MAX_EVENT_SIZE, proxy_listen_handler);
	event_base_add_stream(proxy_listen_stream);

	proxy_int_bus = (busmail_connection_t*) dect_bus;

	// Add rawmail protocol parser for data from Natalie.
	proxy_list = list_new();
	mailProto.add_handler(proxy_int_bus, rawmail_rx_from_natalie);

	printf("Rawmail proxy listening for connection...\n");

	return 0;
}

