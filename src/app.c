#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>


#include <Api/FpGeneral/ApiFpGeneral.h>
#include <Api/FpCc/ApiFpCc.h>
#include <Api/FpMm/ApiFpMm.h>
#include <Api/ProdTest/ApiProdTest.h>
#include <Api/ProdTest/ApiProdTest.h>
#include <RosPrimitiv.h>
#include <Api/RsStandard.h>
#include <termios.h>


#include "tty.h"
#include "error.h"
#include "state.h"
#include "boot.h"
#include "util.h"
#include "app.h"
#include "buffer.h"
#include "busmail.h"
#include "eap.h"


#define INBUF_SIZE 5000

buffer_t * buf;
static int reset_ind = 0;
extern void * client_bus;
extern int client_connected;
void * dect_bus;


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

}


static void application_frame(packet_t *p) {
	
	int i;
	busmail_t * m = (busmail_t *) &p->data[0];
	
	/* Send packets to connected clients */
	if ( client_connected == 1 ) {
		printf("send to client_bus\n");
		packet_dump(p);
		eap_send(client_bus, &p->data[3], p->size);
	}

	switch (m->mail_header) {
		
	case API_FP_RESET_IND:

		printf("API_FP_RESET_IND\n");
		
		if (reset_ind == 0) {
			reset_ind = 1;

			printf("\nWRITE: API_FP_GET_FW_VERSION_REQ\n");
			ApiFpGetFwVersionReqType m1 = { .Primitive = API_FP_GET_FW_VERSION_REQ, };
			busmail_send(dect_bus, (uint8_t *)&m1, sizeof(ApiFpGetFwVersionReqType));

		} else {

		}

		break;

	case API_PROD_TEST_CFM:
		printf("API_PROD_TEST_CFM\n");
		break;

	case RTX_EAP_HW_TEST_CFM:
		printf("RTX_EAP_HW_TEST_CFM\n");
		break;

	case API_FP_GET_FW_VERSION_CFM:
		printf("API_FP_GET_FW_VERSION_CFM\n");
		
		/* Setup terminal id */
		ApiFpCcFeaturesReqType fr = { .Primitive = API_FP_FEATURES_REQ,
					      .ApiFpCcFeature = API_FP_CC_EXTENDED_TERMINAL_ID_SUPPORT };
		busmail_send(dect_bus, (uint8_t *)&fr, sizeof(ApiFpCcFeaturesReqType));
		break;

	case API_FP_FEATURES_CFM:
		printf("API_FP_FEATURES_CFM\n");

		/* Start protocol */
		printf("\nWRITE: API_FP_MM_START_PROTOCOL_REQ\n");
		ApiFpMmStartProtocolReqType r =  { .Primitive = API_FP_MM_START_PROTOCOL_REQ, };
		busmail_send(dect_bus, (uint8_t *)&r, sizeof(ApiFpMmStartProtocolReqType));

		/* Start registration */
		/* printf("\nWRITE: API_FP_MM_SET_REGISTRATION_MODE_REQ\n"); */
		/* ApiFpMmSetRegistrationModeReqType r2 = { .Primitive = API_FP_MM_SET_REGISTRATION_MODE_REQ, \ */
		/* 					.RegistrationEnabled = true, .DeleteLastHandset = false}; */
		/* busmail_send(dect_bus, (uint8_t *)&r2, sizeof(ApiFpMmStartProtocolReqType)); */
		break;

	case API_SCL_STATUS_IND:
		printf("API_SCL_STATUS_IND\n");
		break;


	case API_FP_MM_SET_REGISTRATION_MODE_CFM:
		printf("API_FP_MM_SET_REGISTRATION_MODE_CFM\n");
		break;
	}

	
}





void init_app_state(int dect_fd, config_t * config) {
	
	printf("APP_STATE\n");

	tty_set_raw(dect_fd);
	tty_set_baud(dect_fd, B115200);

	printf("DECT TX TO BRCM RX\n");
	if(gpio_control(118, 0)) return;

	printf("RESET_DECT\n");
	if(dect_chip_reset()) return;

	/* Init busmail subsystem */
	dect_bus = busmail_new(dect_fd, application_frame);
	
	return;
}


void handle_app_package(event_t *e) {

	uint8_t header;

	//util_dump(e->in, e->incount, "\n[READ]");

	/* Add input to busmail subsystem */
	if (busmail_write(dect_bus, e) < 0) {
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
