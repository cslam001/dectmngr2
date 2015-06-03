#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>


#include <Api/FpGeneral/ApiFpGeneral.h>
#include <Api/CodecList/ApiCodecList.h>
#include <Api/FpCc/ApiFpCc.h>
#include <Api/FpMm/ApiFpMm.h>
#include <Api/ProdTest/ApiProdTest.h>
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


#define INBUF_SIZE 5000

buffer_t * buf;
static int reset_ind = 0;

ApiCallReferenceType incoming_call;
ApiCallReferenceType outgoing_call;


#ifndef RSOFFSETOF
/*! \def RSOFFSETOF(type, field)                                                                                                                        
 * Computes the byte offset of \a field from the beginning of \a type. */
#define RSOFFSETOF(type, field) ((size_t)(&((type*)0)->field))
#endif

#define SINGLE_CODECLIST_LENGTH         (sizeof(ApiCodecListType))
#define NBWB_CODECLIST_LENGTH           (SINGLE_CODECLIST_LENGTH + sizeof(ApiCodecInfoType))

/* Codecs */
char nbwbCodecList[NBWB_CODECLIST_LENGTH]={0x01, 0x02, 0x03, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00, 0x04};
char nbCodecList[]={0x01, 0x01, 0x02, 0x00, 0x00, 0x04};
char wbCodecList[]={0x01, 0x01, 0x03, 0x00, 0x00, 0x01};
rsuint8 NarrowCodecArr[30];
rsuint8 WideCodecArr[30];
ApiInfoElementType * NarrowBandCodecIe = (ApiInfoElementType*) NarrowCodecArr;
const rsuint16 NarrowBandCodecIeLen = (RSOFFSETOF(ApiInfoElementType, IeData) + 6);




static void dect_conf_init(void)
{
	NarrowBandCodecIe->Ie = API_IE_CODEC_LIST;
	NarrowBandCodecIe->IeLength = 6;
	NarrowBandCodecIe->IeData[0] = 0x01; // NegotiationIndicator, Negotiation possible
	NarrowBandCodecIe->IeData[1] = 0x01; // NoOfCodecs
	NarrowBandCodecIe->IeData[2] = 0x02; // API_CT_G726 API_MDS_1_MD
	NarrowBandCodecIe->IeData[3] = 0x00;  // MacDlcService
	NarrowBandCodecIe->IeData[4] = 0x00;  // CplaneRouting  API_CPR_CS
	NarrowBandCodecIe->IeData[5] = 0x04;  // SlotSize API_SS_FS fullslot

	/* WideBandCodecIe->Ie = API_IE_CODEC_LIST; */
	/* WideBandCodecIe->IeLength  = 6; */
	/* WideBandCodecIe->IeData[0] = 0x01; */
	/* WideBandCodecIe->IeData[1] = 0x01; */
	/* WideBandCodecIe->IeData[2] = 0x03; */
	/* WideBandCodecIe->IeData[3] = 0x00; */
	/* WideBandCodecIe->IeData[4] = 0x00; */
	/* WideBandCodecIe->IeData[5] = 0x01; */

	return;
}



/* Handset answers */
static void connect_ind(busmail_t *m) {
	
	ApiFpCcConnectIndType * p = (ApiFpCcSetupCfmType *) &m->mail_header;

	printf("handset: %d\n", p->CallReference.Instance.Fp);

	
	ApiFpCcConnectReqType * req = (ApiFpCcConnectReqType*) malloc((sizeof(ApiFpCcConnectReqType) - 1 + NarrowBandCodecIeLen));

        req->Primitive = API_FP_CC_CONNECT_REQ;
	req->CallReference = incoming_call;
        req->InfoElementLength = NarrowBandCodecIeLen;
        memcpy(req->InfoElement,(rsuint8*)NarrowBandCodecIe,NarrowBandCodecIeLen);

	busmail_send((uint8_t *) req, sizeof(ApiFpCcConnectReqType) - 1 + NarrowBandCodecIeLen);
	free(req);

	/* ApiFpCcConnectResType res = { */
	/* 	.Primitive = API_FP_CC_CONNECT_RES, */
	/* 	.CallReference = p->CallReference, */
	/* 	.Status = RSS_SUCCESS, */
	/* 	.InfoElementLength = 0, */
	/* }; */
	
	/* printf("API_FP_CC_CONNECT_RES\n"); */

}



static void setup_cfm(busmail_t *m) {
	
	ApiFpCcSetupCfmType * p = (ApiFpCcSetupCfmType *) &m->mail_header;

	printf("handset: %d\n", p->CallReference.Instance.Fp);
}


/* Caller dials */
static void setup_ind(busmail_t *m) {

	ApiFpCcSetupIndType * p = (ApiFpCcSetupIndType *) &m->mail_header;
	
	ApiFpCcAudioIdType Audio, OutAudio;

	printf("CallReference: %d\n", p->CallReference);
	printf("TerminalIdInitiating: %d\n", p->TerminalId);
	
	incoming_call = p->CallReference;
	
	/* Reply to initiating handset */

	ApiFpCcSetupResType res = {
		.Primitive = API_FP_CC_SETUP_RES,
		.CallReference = incoming_call,
		.Status = RSS_SUCCESS,
		.AudioId.IntExtAudio = API_IEA_INT,
		.AudioId.SourceTerminalId = p->TerminalId,
	};

	printf("API_FP_CC_SETUP_RES\n");
	busmail_send((uint8_t *)&res, sizeof(ApiFpCcSetupResType));

	/* Connection request to dialed handset */
	ApiFpCcSetupReqType req = {
		.Primitive = API_FP_CC_SETUP_REQ,
		.TerminalId = 2,
		.AudioId.SourceTerminalId = 2,
		.AudioId.IntExtAudio = API_IEA_INT,
		.BasicService = API_BASIC_SPEECH,
		.CallClass = API_CC_NORMAL,
		.Signal = API_CC_SIGNAL_ALERT_ON_PATTERN_2,
		.InfoElementLength = 0,
	};

	printf("API_FP_CC_SETUP_REQ\n");
	busmail_send((uint8_t *)&req, sizeof(ApiFpCcSetupReqType));

	return;
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


static void application_frame(busmail_t *m) {
	
	int i;

	switch (m->mail_header) {
		
	case API_FP_RESET_IND:
		printf("API_FP_RESET_IND\n");
		

		if (reset_ind == 0) {
			reset_ind = 1;

			printf("\nWRITE: API_FP_GET_FW_VERSION_REQ\n");
			ApiFpGetFwVersionReqType m1 = { .Primitive = API_FP_GET_FW_VERSION_REQ, };
			busmail_send((uint8_t *)&m1, sizeof(ApiFpGetFwVersionReqType));

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
		busmail_send((uint8_t *)&fr, sizeof(ApiFpCcFeaturesReqType));
		break;

	case API_FP_FEATURES_CFM:
		printf("API_FP_FEATURES_CFM\n");

		/* Start protocol */
		printf("\nWRITE: API_FP_MM_START_PROTOCOL_REQ\n");
		ApiFpMmStartProtocolReqType r =  { .Primitive = API_FP_MM_START_PROTOCOL_REQ, };
		busmail_send((uint8_t *)&r, sizeof(ApiFpMmStartProtocolReqType));

		/* Start registration */
		printf("\nWRITE: API_FP_MM_SET_REGISTRATION_MODE_REQ\n");
		ApiFpMmSetRegistrationModeReqType r2 = { .Primitive = API_FP_MM_SET_REGISTRATION_MODE_REQ, \
							.RegistrationEnabled = true, .DeleteLastHandset = false};
		busmail_send((uint8_t *)&r2, sizeof(ApiFpMmStartProtocolReqType));
		break;


	case API_SCL_STATUS_IND:
		printf("API_SCL_STATUS_IND\n");
		break;


	case API_FP_MM_SET_REGISTRATION_MODE_CFM:
		printf("API_FP_MM_SET_REGISTRATION_MODE_CFM\n");
		break;

	case API_FP_CC_SETUP_IND:
		printf("API_FP_CC_SETUP_IND\n");
		setup_ind(m);
		break;

	case API_FP_CC_SETUP_REQ:
		printf("API_FP_CC_SETUP_REQ\n");
		break;

	case API_FP_CC_RELEASE_IND:
		printf("API_FP_CC_RELEASE_IND\n");
		break;

	case API_FP_CC_SETUP_CFM:
		printf("API_FP_CC_SETUP_CFM\n");
		setup_cfm(m);
		break;

	case API_FP_CC_REJECT_IND:
		printf("API_FP_CC_REJECT_IND\n");
		break;

	case API_FP_CC_CONNECT_IND:
		printf("API_FP_CC_CONNECT_IND\n");
		connect_ind(m);
		break;

		
	}
}





void init_app_state(int dect_fd, config_t * config) {
	
	printf("APP_STATE\n");

	tty_set_raw(dect_fd);
	tty_set_baud(dect_fd, B115200);

	printf("DECT TX TO BRCM RX\n");
	system("/sbin/brcm_fw_tool set -x 118 -p 0 > /dev/null");

	printf("RESET_DECT\n");
	system("/usr/bin/dect-reset > /dev/null");

	/* Init input buffer */
	buf = buffer_new(500);
	
	/* Init busmail subsystem */
	busmail_init(dect_fd, application_frame);
	
}


void handle_app_package(event_t *e) {

	uint8_t header;
	packet_t packet;
	packet_t *p = &packet;
	p->fd = e->fd;
	p->size = 0;

	//util_dump(e->in, e->incount, "\n[READ]");

	/* Add input to buffer */
	if (buffer_write(buf, e->in, e->incount) == 0) {
		printf("buffer full\n");
	}
	
	/* Process whole packets in buffer */
	while(busmail_get(p, buf) == 0) {
		busmail_dispatch(p);
	}
}





struct state_handler app_handler = {
	.state = APP_STATE,
	.init_state = init_app_state,
	.event_handler = handle_app_package,
};

struct state_handler * app_state = &app_handler;
