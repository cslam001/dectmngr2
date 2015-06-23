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
#define HEADER_OFFSET 10


buffer_t * buf;
static int reset_ind = 0;
void * bus;
void * dect_stream, * dect_bus;
int dect_fd;

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

			/* /\* Set hard coded RFPI 0x02, 0x3f, 0x80, 0x00, 0xf8 *\/ */
			/* uint8_t data[] = {0x66, 0xf0, 0x00, 0x00, 0x00, 0x01, 0x10, 0x00, \ */
			/* 	  0x00, 0x00, 0x00, 0x00, 0x0b, 0x02, 0x3f, 0x80, \ */
			/* 	  0x00, 0xf8, 0x25, 0xc0, 0x01, 0x00, 0xf8, 0x23}; */

			/* Set hard coded RFPI 0x02, 0x3f, 0x80, 0x00, 0xf8 */
			/* Applicaton mail type */
			/* Something */
			/* Command */
			/* Address */
			/* Size */
			/* Data */

			uint8_t data[] = {0x66, 0xf0, \
					  0x00, 0x00, 0x00, 0x01, \
					  0x10, 0x00, \
					  0x00, 0x00, 0x00, 0x00, \
					  0x0b, \
					  0x02, 0x3f, 0x90, 0x00, \
					  0xf8, 0x25, 0xc0, 0x01, \
					  0x00, 0xf8, 0x23};

			/* uint8_t data[] = {0x66, 0xf0, \ */
			/* 		  0x00, 0x00, 0x00, 0x01, \ */
			/* 		  0x10, 0x00, \  */
			/* 		  0x80, 0x00, 0x00, 0x00, \  */
			/* 		  0x01, \ */
			/* 		  0x01 }; */




			busmail_send_task(dect_bus, data, sizeof(data), 0);
		}

		break;

	case PT_CMD_SET_NVS:
		printf("PT_CMD_SET_NVS\n");
		printf("Get NVS\n");
		uint8_t data1[] = {0x66, 0xf0, 0x00, 0x00, 0x01, 0x01, 0x05, 0x00, \
				   0x00, 0x00, 0x00, 0x00, 0xff};
		busmail_send_task(dect_bus, data1, sizeof(data1), 0);
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
		
		busmail_ack(dect_bus);
		exit(0);
		break;
	}
		
}


void rfpi_handler(packet_t *p) {
	
	int i;
	busmail_t * m = (busmail_t *) &p->data[0];

	switch (m->mail_header) {
		
	case API_FP_RESET_IND:

		if (reset_ind == 0) {
			reset_ind = 1;

			printf("\nWRITE: API_FP_GET_FW_VERSION_REQ\n");
			ApiFpGetFwVersionReqType m1 = { .Primitive = API_FP_GET_FW_VERSION_REQ, };
			busmail_send(dect_bus, (uint8_t *)&m1, sizeof(ApiFpGetFwVersionReqType));
		}

		break;

	case RTX_EAP_HW_TEST_CFM:
		rtx_eap_hw_test_cfm(m);
		break;

	case API_FP_GET_FW_VERSION_CFM:
		fw_version_cfm(m);

		printf("\nWRITE: NvsDefault\n");
		uint8_t data[] = {0x66, 0xf0, 0x00, 0x00, 0x02, 0x01, 0x01, 0x00, 0x01};
		busmail_send_task(dect_bus, data, sizeof(data), 0);

		/* printf("Get NVS\n"); */
		/* uint8_t data1[] = {0x66, 0xf0, 0x00, 0x00, 0x01, 0x01, 0x05, 0x00, \ */
		/* 		   0x00, 0x00, 0x00, 0x00, 0xff}; */
		/* busmail_send_task(data1, sizeof(data1), 0); */
		break;

	}
}




void nvs_handler(void * stream, void * event) {

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


void rfpi_init(void * bus) {

	printf("rfpi_init\n");
	busmail_add_handler(bus, rfpi_handler);
}

void nvs_init(void * event_base, config_t * config) {
	
	printf("nvs_init\n");

	/* Setup dect tty */
	dect_fd = tty_open("/dev/ttyS1");
	tty_set_raw(dect_fd);
	tty_set_baud(dect_fd, B115200);

	/* Register dect stream */
	dect_stream = stream_new(dect_fd);
	stream_add_handler(dect_stream, nvs_handler);
	event_base_add_stream(event_base, dect_stream);

	/* Init busmail subsystem */
	dect_bus = busmail_new(dect_fd);

	/* Initialize submodules. The submodules will bind 
	   application frame handlers to the dect_bus */
	//connection_init(dect_bus);
	api_parser_init(dect_bus);
	rfpi_init(dect_bus);
	

	/* Connect and reset dect chip */
	printf("DECT TX TO BRCM RX\n");
	if(gpio_control(118, 0)) return;
 	printf("RESET_DECT\n");
	if(dect_chip_reset()) return;
}


