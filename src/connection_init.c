#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

#include "connection_init.h"
#include "busmail.h"
#include "util.h"

#include <Api/FpGeneral/ApiFpGeneral.h>
#include <Api/CodecList/ApiCodecList.h>
#include <Api/FpCc/ApiFpCc.h>
#include <Api/FpMm/ApiFpMm.h>
#include <Api/ProdTest/ApiProdTest.h>
#include <Api/FpAudio/ApiFpAudio.h>
#include <Api/RsStandard.h>

#define TRUE 1
#define FALSE 0

static int reset_ind = 0;
void * dect_bus;





//-------------------------------------------------------------
static void fw_version_cfm(busmail_t *m) {

	ApiFpGetFwVersionCfmType * p = (ApiFpGetFwVersionCfmType *) &m->mail_header;

	printf("fw_version_cfm\n");
	
	if (p->Status == RSS_SUCCESS) {
		printf("Status: RSS_SUCCESS\n");
	} else {
		printf("Status: RSS_FAIL: %x\n", p->Status);
	}

	printf("VersionHex %x\n", (unsigned int)p->VersionHex);
	
	if (p->DectType == API_EU_DECT) {
		printf("DectType: API_EU_DECT\n");
	} else {
		printf("DectType: BOGUS\n");
	}

	return;
}



//-------------------------------------------------------------
static void connection_init_handler(packet_t *p) {
	
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
		;

		/* Init PCM bus */
		printf("\nWRITE: API_FP_INIT_PCM_REQ\n");
		ApiFpInitPcmReqType pcm_req =  { .Primitive = API_FP_INIT_PCM_REQ,
						 .PcmEnable = 0x1,
						 .IsMaster = 0x0,
						 .DoClockSync = 0x1,
						 .PcmFscFreq = AP_FSC_FREQ_8KHZ,  // PCM FS 16/8 Khz select (1 - 16Khz, 0 - 8Khz)
						 .PcmFscLength = AP_FSC_LENGTH_NORMAL,
						 .PcmFscStartAligned = 0x0,
						 .PcmClk = 0x0,    /* Ignored if device is slave */
						 .PcmClkOnRising = 0x0,
						 .PcmClksPerBit = 0x1,
						 .PcmFscInvert = 0x0,
						 .PcmCh0Delay = 0x0,
						 .PcmDoutIsOpenDrain = 0x1, /* Must be 1 if mult. devices on bus */
						 .PcmIsOpenDrain = 0x0,  /* 0 == Normal mode */
		};
		busmail_send(dect_bus, (uint8_t *)&pcm_req, sizeof(ApiFpInitPcmReqType));
		break;

	case API_FP_INIT_PCM_CFM:
		
		;
		ApiFpInitPcmCfmType * p = (ApiFpInitPcmCfmType *) &m->mail_header;
		print_status(p->Status);

		/* Start protocol */
		connection_set_state(1);
		break;
	}
}



//-------------------------------------------------------------
void connection_init(void * bus) {

	dect_bus = bus;
	busmail_add_handler(bus, connection_init_handler);
}



//-------------------------------------------------------------
/* Start or stop protocol (the DECT radio) */
int connection_set_state(int onoff) {
	if(onoff) {
		printf("\nWRITE: API_FP_MM_START_PROTOCOL_REQ\n");
		ApiFpMmStartProtocolReqType r =  { .Primitive = API_FP_MM_START_PROTOCOL_REQ, };
		busmail_send(dect_bus, (uint8_t *)&r, sizeof(ApiFpMmStartProtocolReqType));
	}
	else {
		ApiFpMmStopProtocolReqType r = { .Primitive = API_FP_MM_STOP_PROTOCOL_REQ, };
		busmail_send(dect_bus, (uint8_t *)&r, sizeof(ApiFpMmStopProtocolReqType));
	}

	return 0;
}

