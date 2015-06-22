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
static int reset_ind = 0;
int client_connected;
void * dect_bus;
void * dect_stream, * listen_stream;
void * client_stream;
int epoll_fd;
void * client_list;
void * client_bus;
void * client_list;
void * event_base;
struct sigaction act;



static void list_connected(int fd) {
	printf("connected fd:s : %d\n", fd);
}


void sighandler(int signum, siginfo_t * info, void * ptr) {

	printf("Recieved signal %d\n", signum);
}




void eap(packet_t *p) {
	
	int i;

	printf("send to dect_bus\n");
	packet_dump(p);
	
	busmail_send0(dect_bus, &p->data[3], p->size - 3);
	
	/* /\* For RSX *\/ */
	/* busmail_send_prog(dect_bus, &p->data[3], p->size - 3, 0x81); */
}



static void fw_version_cfm(busmail_t *m) {

	ApiFpGetFwVersionCfmType * p = (ApiFpGetFwVersionCfmType *) &m->mail_header;

	printf("fw_version_cfm\n");
	
	if (p->Status == RSS_SUCCESS) {
		printf("Status: RSS_SUCCESS\n");
	} else {
		printf("Status: RSS_FAIL: %x\n", p->Status);
	}

	printf("VersionHex %x\n", (uint)p->VersionHex);
	
	if (p->DectType == API_EU_DECT) {
		printf("DectType: API_EU_DECT\n");
	} else {
		printf("DectType: BOGUS\n");
	}

	return;
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


static void api_packet_parser(packet_t *p) {

	busmail_t * m = (busmail_t *) &p->data[0];

	/* Application command */
	switch (m->mail_header) {

	case API_FP_RESET_IND:
		printf("API_FP_RESET_IND\n");
		break;

	case API_PROD_TEST_CFM:
		printf("API_PROD_TEST_CFM\n");
		break;

	case RTX_EAP_HW_TEST_CFM:
		printf("RTX_EAP_HW_TEST_CFM\n");
		break;

	case API_FP_GET_FW_VERSION_CFM:
		printf("API_FP_GET_FW_VERSION_CFM\n");
		break;
	
	case API_FP_FEATURES_CFM:
		printf("API_FP_FEATURES_CFM\n");
		break;

	case API_SCL_STATUS_IND:
		printf("API_SCL_STATUS_IND\n");
		break;

	case API_FP_MM_SET_REGISTRATION_MODE_CFM:
		printf("API_FP_MM_SET_REGISTRATION_MODE_CFM\n");
		break;

	case API_FP_CC_SETUP_IND:
		printf("API_FP_CC_SETUP_IND\n");
		break;

	case API_FP_CC_SETUP_REQ:
		printf("API_FP_CC_SETUP_REQ\n");
		break;

	case API_FP_CC_RELEASE_IND:
		printf("API_FP_CC_RELEASE_IND\n");
		break;

	case API_FP_CC_RELEASE_CFM:
		printf("API_FP_CC_RELEASE_CFM\n");
		break;

	case API_FP_CC_SETUP_CFM:
		printf("API_FP_CC_SETUP_CFM\n");
		break;

	case API_FP_CC_REJECT_IND:
		printf("API_FP_CC_REJECT_IND\n");
		break;

	case API_FP_CC_CONNECT_IND:
		printf("API_FP_CC_CONNECT_IND\n");
		break;
		
	case API_FP_CC_CONNECT_CFM:
		printf("API_FP_CC_CONNECT_CFM\n");
		break;
		
	case API_FP_CC_ALERT_IND:
		printf("API_FP_CC_ALERT_IND\n");
		break;
		
	case API_FP_CC_ALERT_CFM:
		printf("API_FP_CC_ALERT_CFM\n");
		break;
		
	case API_FP_CC_SETUP_ACK_CFM:
		printf("API_FP_CC_SETUP_ACK_CFM\n");
		break;
		
	case API_FP_CC_INFO_IND:
		printf("API_FP_CC_INFO_IND\n");
		break;

	default:
		printf("unknown application frame\n");
		break;
	}
}


static void busmail_init_handler(packet_t *p) {
	
	int i;
	busmail_t * m = (busmail_t *) &p->data[0];


	if (m->task_id != 1) return;
	
	/* Application command */
	switch (m->mail_header) {
		
	case API_FP_RESET_IND:
		
		if (reset_ind == 0) {
			reset_ind = 1;

			printf("\nWRITE: API_FP_GET_FW_VERSION_REQ\n");
			ApiFpGetFwVersionReqType m1 = { .Primitive = API_FP_GET_FW_VERSION_REQ, };
			busmail_send(dect_bus, (uint8_t *)&m1, sizeof(ApiFpGetFwVersionReqType));

		} else {

		}

		break;

	case API_FP_GET_FW_VERSION_CFM:
		/* Setup terminal id */
		printf("\nWRITE: API_FP_FEATURES_REQ\n");
		ApiFpCcFeaturesReqType fr = { .Primitive = API_FP_FEATURES_REQ,
					      .ApiFpCcFeature = API_FP_CC_EXTENDED_TERMINAL_ID_SUPPORT };
	
		busmail_send(dect_bus, (uint8_t *)&fr, sizeof(ApiFpCcFeaturesReqType));
		break;

	case API_FP_FEATURES_CFM:

		/* Start protocol */
		printf("\nWRITE: API_FP_MM_START_PROTOCOL_REQ\n");
		ApiFpMmStartProtocolReqType r =  { .Primitive = API_FP_MM_START_PROTOCOL_REQ, };
		busmail_send(dect_bus, (uint8_t *)&r, sizeof(ApiFpMmStartProtocolReqType));
		break;
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
		//list_each(client_list, list_connected);

		/* Destroy client connection object here */

	} else {

		/* Data is read from client */
		util_dump(event_data(event), event_count(event), "[CLIENT]");

		/* Send packets from clients to dect_bus */
		eap_write(client_bus, event);
		eap_dispatch(client_bus);

	}

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



static int setup_listener(void) {

	int listen_fd, opt = 1;
	struct sockaddr_in my_addr, peer_addr;
	socklen_t peer_addr_size;
 
	memset(&my_addr, 0, sizeof(my_addr));
	my_addr.sin_family = AF_INET;
	my_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	my_addr.sin_port = htons(10468);
	
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


static void setup_signal_handler(void) {

	/* Setup signal handler. When writing data to a
	   client that closed the connection we get a 
	   SIGPIPE. We need to catch it to avoid being killed */
	memset(&act, 0, sizeof(act));
	act.sa_sigaction = sighandler;
	act.sa_flags = SA_SIGINFO;
	sigaction(SIGPIPE, &act, NULL);
}


void init_app_state(void * event_b, config_t * config) {
	
	int dect_fd, listen_fd;

	printf("APP_STATE\n");
	event_base = event_b;

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


	/* Initialize submodules. The submodules will bind application frame
	 handlers to the dect_bus */
	internal_call_init(dect_bus);

	busmail_add_handler(dect_bus, busmail_init_handler);
	busmail_add_handler(dect_bus, api_packet_parser);

	busmail_add_handler(dect_bus, client_packet_handler);
	

	/* Init client list */
	client_list = list_new();

	/* Setup listening socket */
	listen_fd = setup_listener();
	listen_stream = stream_new(listen_fd);
	stream_add_handler(listen_stream, listen_handler);
	event_base_add_stream(event_base, listen_stream);



	/* Connect and reset dect chip */
	printf("DECT TX TO BRCM RX\n");
	if(gpio_control(118, 0)) return;
 	printf("RESET_DECT\n");
	if(dect_chip_reset()) return;


	return;
}


void handle_app_package(void * event) {

	uint8_t header;

	//util_dump(e->in, e->incount, "\n[READ]");

	/* Add input to busmail subsystem */
	if (busmail_write(dect_bus, event) < 0) {
		printf("busmail buffer full\n");
	}
	
	/* Process whole packets in buffer. The previously registered
	   callback will be called for application frames */
	busmail_dispatch(dect_bus);

	return;
}





struct state_handler app_handler = {
	.state = APP_STATE,
	.init_state = init_app_state,
	.event_handler = handle_app_package,
};

struct state_handler * app_state = &app_handler;
