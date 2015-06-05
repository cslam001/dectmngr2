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
#include "util.h"
#include "buffer.h"
#include "busmail.h"


#define PT_CMD_SET_ID 0x001B
#define PT_CMD_GET_ID 0x001C
#define PT_CMD_SET_NVS 0x0100
#define PT_CMD_GET_NVS 0x0101
#define PT_CMD_NVS_DEFAULT 0x0102
#define PT_CMD_SET_TESTMODE 0x0000

#define HEADER_OFFSET 10

buffer_t * buf;
static int reset_ind = 0;
static config_t * test_config;
void * bus;

typedef struct __attribute__((__packed__))
{
	uint16_t TestPrimitive;
	uint16_t size;
	uint8_t data[0];
} rtx_eap_hw_test_cfm_t;

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


static void rtx_eap_hw_test_cfm(busmail_t *m) {
	
	rtx_eap_hw_test_cfm_t * t = (rtx_eap_hw_test_cfm_t *) m->mail_data;
	int i;
	
	switch (t->TestPrimitive) {

	case PT_CMD_NVS_DEFAULT:
		printf("PT_CMD_NVS_DEFAULT\n");
		
		if (t->data[0] == RSS_PENDING) {

			printf("nvs_default: pending\n");

		} else if (t->data[0] == RSS_SUCCESS) {

			printf("nvs_default: ok\n");

			printf("Set NVS\n");
			uint8_t data[] = {0x66, 0xf0, 0x00, 0x00, 0x01, 0x01, 0x05, 0x00, \
				   0x00, 0x00, 0x00, 0x00, 0xff};


			
			busmail_send0(bus, data, sizeof(data));
		}

		break;

	case PT_CMD_SET_NVS:
		printf("PT_CMD_SET_NVS\n");
		printf("Get NVS\n");
		uint8_t data1[] = {0x66, 0xf0, 0x00, 0x00, 0x01, 0x01, 0x05, 0x00, \
				   0x00, 0x00, 0x00, 0x00, 0xff};
		busmail_send0(bus, data1, sizeof(data1));
		break;

	case PT_CMD_GET_NVS:
		printf("PT_CMD_GET_NVS\n");

		printf("RFPI:\t\t");
		for ( i = 0; i < 5; i++) {
			printf("0x%02x ", m->mail_data[HEADER_OFFSET + i]);
		}
		printf("\n");

		printf("TEST MODE:\t");
		printf("0x%02x ", m->mail_data[HEADER_OFFSET + 0x80]);
		printf("\n");

		busmail_ack(bus);
		exit(0);
		break;

	case PT_CMD_SET_TESTMODE:
		printf("PT_CMD_SET_TESTMODE\n");
		break;
	}

		
}


static void application_frame(busmail_t *m) {
	
	int i;

	switch (m->mail_header) {
		
	case API_FP_RESET_IND:
		printf("API_FP_RESET_IND\n");

		if (reset_ind == 0) {
			reset_ind = 1;

			printf("\nWRITE: API_FP_GET_FW_VERSION_REQ\n");
			ApiFpGetFwVersionReqType m1 = { .Primitive = API_FP_GET_FW_VERSION_REQ, };
			busmail_send(bus, (uint8_t *)&m1, sizeof(ApiFpGetFwVersionReqType));
		}

		break;

	case RTX_EAP_HW_TEST_CFM:
		printf("RTX_EAP_HW_TEST_CFM\n");
		rtx_eap_hw_test_cfm(m);
		break;

	case API_FP_GET_FW_VERSION_CFM:
		printf("API_FP_GET_FW_VERSION_CFM\n");
		fw_version_cfm(m);

		printf("\nWRITE: Testmode Enable\n");

		/* Testmode enable */
		//		uint8_t data[] = {0x66, 0xf0, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01};
		printf("\nSet NVS\n");

		if (test_config->test_enable) {
			uint8_t enable[] = {0x66, 0xf0,	  \
					  0x00, 0x00, 0x00, 0x01, \
					  0x10, 0x00, \ 
					  0x80, 0x00, 0x00, 0x00, \ 
					  0x01, \
					  0x01 };
			busmail_send0(bus, enable, sizeof(enable));
		} else {

			uint8_t disable[] = {0x66, 0xf0,		  \
					  0x00, 0x00, 0x00, 0x01, \
					  0x10, 0x00, \ 
					  0x80, 0x00, 0x00, 0x00, \ 
					  0x01, \
					  0x00 };
			busmail_send0(bus, disable, sizeof(disable));


		}


		break;

	case API_SCL_STATUS_IND:
		printf("API_SCL_STATUS_IND\n");
		break;
	}
}


void init_test_state(int dect_fd, config_t * config) {
	
	printf("TEST_STATE\n");

	test_config = config;
	
	if ( test_config->test_enable ) {
		printf("enable test\n");
	} else {
		printf("disable test\n");
	}

	tty_set_raw(dect_fd);
	tty_set_baud(dect_fd, B115200);

	printf("DECT TX TO BRCM RX\n");
	if(gpio_control(118, 0)) return;

	printf("RESET_DECT\n");
	if(dect_chip_reset()) return;

	/* Init busmail subsystem */
	bus = busmail_new(dect_fd, application_frame);
}


void handle_test_package(event_t *e) {

	uint8_t header;
	packet_t packet;
	packet_t *p = &packet;
	p->fd = e->fd;
	p->size = 0;

	//util_dump(e->in, e->incount, "\n[READ]");

	/* Add input to busmail subsystem */
	if (busmail_write(bus, e) < 0) {
		printf("busmail buffer full\n");
	}
	
	/* Process whole packets in buffer. The previously registered
	   callback will be called for application frames */
	busmail_dispatch(bus);

	return;
}





struct state_handler test_handler = {
	.state = TEST_STATE,
	.init_state = init_test_state,
	.event_handler = handle_test_package,
};

struct state_handler * test_state = &test_handler;
