
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

#include <Api/FpGeneral/ApiFpGeneral.h>
#include <Api/FpCc/ApiFpCc.h>
#include <Api/FpMm/ApiFpMm.h>
#include <Api/RsStandard.h>
#include <Api/FpUle/ApiFpUle.h>



//-------------------------------------------------------------
const ApiFpMmSetRegistrationModeCfmType fakeRegistrationModeCfm = {
	.Primitive = API_FP_MM_SET_REGISTRATION_MODE_CFM,
	.Status = RSS_SUCCESS
};

const ApiFpUleInitCfmType fakeUleInitCfm = {
	.Primitive = API_FP_ULE_INIT_CFM,
	.Status = RSS_SUCCESS,
	.MaxUlpDevices = 58,
	.UpLinkBuffers = 159
};


//-------------------------------------------------------------
static int listenFd;
static int client_connected;
static void *proxy_list;
static void *proxy_stream;
static eap_connection_t *proxy_bus;
static void *proxy_listen_stream;
static busmail_connection_t *proxy_int_bus;

static packet_t *packetBuf;
static int packRd;
static int packWr;
static int packToken;


//-------------------------------------------------------------
static void rawmail_rx_from_client_enqueue(packet_t *p);
static void dequeue_rawmail_from_client(void);
static void rawmail_rx_from_client(packet_t *p);
static void rawmail_rx_from_natalie(packet_t *p);




//-------------------------------------------------------------
static void rawmail_rx_from_client_enqueue(packet_t *p) {
	if(packToken && packRd == packWr) {
		printf("Proxy direct > dect\n");
		rawmail_rx_from_client(p);
	}
	else {
		printf("Proxy enqueue %d\n", packWr);
		memcpy(packetBuf + packWr, p, sizeof(packet_t));
		packWr++;
		if(packWr == 10) packWr = 0;
	}
}


//-------------------------------------------------------------
static void dequeue_rawmail_from_client(void) {
	if(packRd == packWr) return;
	if(!packToken) return;

	printf("Proxy dequeue %d\n", packRd);
	rawmail_rx_from_client(packetBuf + packRd);
	packRd++;
	if(packRd == 10) packRd = 0;
}


//-------------------------------------------------------------
// We receive rawmail data from third party app
static void rawmail_rx_from_client(packet_t *p) {
	busmail_t *mail, *fakeBusmail;
	uint32_t lenPacket, fakeLen, lenMail;
	const void *fakeCfm;
	uint8_t *data;

	mail = (busmail_t*) p->data;
	data = (uint8_t*) p->data + BUSMAIL_HEADER_SIZE;
	lenPacket = p->size - BUSMAIL_HEADER_SIZE;
lenMail = 0;
	fakeCfm = NULL;
	fakeLen = 0;

	//printf("proxy to dect_bus len %d\n", len);
	switch (mail->mail_header) {
		case API_FP_MM_SET_REGISTRATION_MODE_REQ: {
			ApiFpMmSetRegistrationModeReqType *req = 
				(ApiFpMmSetRegistrationModeReqType*) &mail->mail_header;
lenMail = sizeof(ApiFpMmSetRegistrationModeReqType);
printf("Proxy API_FP_MM_SET_REGISTRATION_MODE_REQ packet %d len should be %d\n", lenPacket, lenMail);

			/* When a user presses the Dect button on the box
			 * we get a collition of events. BOTH dectmngr2 and
			 * the third party ULE application activates
			 * registration. And BOTH maintain separate timers
			 * for when to end the registration and control the
			 * box LED. We try to alleviate the situation by
			 * intercepting messages from the third party app.
			 * If dectmngr2 has already started registration the
			 * third party is blocked from doing it as well. It
			 * can however still modify PIN code. */
			if(connection.registration == INACTIVE) break;
			fakeCfm = &fakeRegistrationModeCfm;
			fakeLen = sizeof(fakeRegistrationModeCfm);
			if(req->RegistrationEnabled) {
				printf("Third party activates registration while busy, ");
				printf("blocking it.\n");
			}
			else {
				printf("Third party inactivates registration while busy, ");
				printf("allowing it though, for consistent behaviour of ");
				printf("LED and PIN.\n");
				connection_set_registration(0);
			}
			break;
		}

		case API_FP_ULE_INIT_REQ:
			fakeCfm = &fakeUleInitCfm;
			fakeLen = sizeof(fakeUleInitCfm);
lenMail = sizeof(ApiFpUleInitReqType);
printf("Proxy API_FP_ULE_INIT_REQ packet %d len should be %d\n", lenPacket, lenMail);
			break;

		case API_FP_ULE_SET_FEATURES_REQ:
lenMail = sizeof(ApiFpUleSetFeaturesReqType);
printf("Proxy API_FP_ULE_SET_FEATURES_REQ packet %d len should be %d\n", lenPacket, lenMail);
			break;

		case API_FP_ULE_GET_REGISTRATION_COUNT_REQ:
lenMail = sizeof(ApiFpUleGetRegistrationCountReqType);
printf("Proxy API_FP_ULE_GET_REGISTRATION_COUNT_REQ packet %d len should be %d\n", lenPacket, lenMail);
			break;

		case API_FP_GET_FW_VERSION_REQ:
lenMail = sizeof(ApiFpGetFwVersionReqType);
printf("Proxy API_FP_GET_FW_VERSION_REQ packet %d len should be %d\n", lenPacket, lenMail);
			break;

		case API_FP_MM_GET_ACCESS_CODE_REQ:
lenMail = sizeof(ApiFpMmGetAccessCodeReqType);
printf("Proxy API_FP_MM_SET_ACCESS_CODE_REQ packet %d len should be %d\n", lenPacket, lenMail);
			break;

		case API_FP_MM_SET_ACCESS_CODE_REQ:
lenMail = sizeof(ApiFpMmSetAccessCodeReqType);
printf("Proxy API_FP_MM_SET_ACCESS_CODE_REQ packet %d len should be %d\n", lenPacket, lenMail);
			break;

		case API_FP_MM_START_PROTOCOL_REQ:
lenMail = sizeof(ApiFpMmStartProtocolReqType);
printf("Proxy API_FP_MM_START_PROTOCOL_REQ packet %d len should be %d\n", lenPacket, lenMail);
			break;

		case API_FP_ULE_SET_PVC_LEGACY_MODE_REQ:
lenMail = sizeof(ApiFpUleSetPvcLegacyModeReqType);
printf("Proxy API_FP_ULE_SET_PVC_LEGACY_MODE_REQ packet %d len should be %d\n", lenPacket, lenMail);
			break;

		case API_FP_ULE_GET_DEVICE_IPUI_REQ:
lenMail = sizeof(ApiFpUleGetDeviceIpuiReqType);
printf("Proxy API_FP_ULE_GET_DEVICE_IPUI_REQ packet %d len should be %d\n", lenPacket, lenMail);
			break;

		case API_FP_ULE_DATA_REQ: {
			ApiFpUleDataReqType *req = (ApiFpUleDataReqType*) &mail->mail_header;
lenMail = sizeof(ApiFpUleDataReqType) + req->Length - 1;
printf("Proxy API_FP_ULE_DATA_REQ %d len should be %d\n", lenPacket, lenMail);
			}
			break;

		case API_FP_ULE_DELETE_REGISTRATION_REQ:
lenMail = sizeof(ApiFpUleDeleteRegistrationReqType);
printf("Proxy API_FP_ULE_DELETE_REGISTRATION_REQ packet %d len should be %d\n", lenPacket, lenMail);
			break;

		case API_FP_MM_DELETE_REGISTRATION_REQ:
lenMail = sizeof(ApiFpMmDeleteRegistrationReqType);
printf("Proxy API_FP_MM_DELETE_REGISTRATION_REQ packet %d len should be %d\n", lenPacket, lenMail);
			break;


		default:
			break;
	}


	/* Send the rawmail through to Natalie or bounce
	 * a fake reply back to third party app? */
	if(fakeLen) {
		packet_t *fakePacket = malloc(sizeof(packet_t));
		fakePacket->fd = p->fd;
		fakePacket->size = BUSMAIL_HEADER_SIZE + fakeLen;
		fakeBusmail = (busmail_t*) fakePacket->data;
		fakeBusmail->frame_header = BUSMAIL_PACKET_HEADER;
		fakeBusmail->program_id = API_PROG_ID;
		fakeBusmail->task_id = API_TASK_ID;
		memcpy(&fakeBusmail->mail_header, fakeCfm, fakeLen);
		printf("We fake a confirm to third party\n");
		rawmail_rx_from_natalie(fakePacket);
		free(fakePacket);
	}
	else if(lenMail) {
		switch (mail->mail_header) {
			case API_FP_MM_START_PROTOCOL_REQ:
			case API_FP_MM_STOP_PROTOCOL_REQ:
				/* We discard these mails from third party apps due
				 * to they don't produce any confirm back. And they
				 * are redundant. */
				break;

			default:
packToken = 0;
printf("Proxy token %d\n", packToken);
			mailProto.send(proxy_int_bus, data, lenMail);
IT LOOKS LIKE ULEAPP IGNORES TO WAIT FOR FpUleDtrInd
BEFORE FIRST PACKET.
AN EMPTY PACKET IS A CONFIGURATION REQUEST FROM SENSOR?
SEND FAKE HANDSET PRESENT IND?
			break;
		}
	}

	lenPacket -= lenMail;
	if(lenPacket) printf("Proxy packet remaining data %d -----------------------------------------------------------------------------\n", lenPacket);
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
printf("Proxy token %d\n", packToken);

		/* Send packets from third party to dect_bus */
		rawmail_dispatch(proxy_bus);
printf("Proxy token %d\n", packToken);
	}
	else {
		util_dump(event_data(event), event_count(event), "[Proxy discard]");
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

packToken = 1;
printf("Proxy token %d\n", packToken);
dequeue_rawmail_from_client();
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
//	rawmail_add_handler(proxy_bus, rawmail_rx_from_client);
rawmail_add_handler(proxy_bus, rawmail_rx_from_client_enqueue);
	client_connected = 1;
	perhaps_disable_radio();

	packetBuf = malloc(sizeof(packet_t)*20);
	packRd = 0;
	packWr = 0;
	packToken = 1;
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

