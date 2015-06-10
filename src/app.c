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
#include "eap.h"


#define INBUF_SIZE 5000

buffer_t * buf;
static int reset_ind = 0;
extern void * client_bus;
extern int client_connected;
void * dect_bus;

ApiCallReferenceType incoming_call;
ApiCallReferenceType outgoing_call;
ApiSystemCallIdType internal_call;

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


static void print_status(RsStatusType s) {

	switch (s) {
		
	case RSS_SUCCESS:
		printf("RSS_SUCCESS\n");
		break;

	case RSS_NOT_SUPPORTED:
		printf("RSS_NOT_SUPPORTED\n");
		break;

	case RSS_BAD_ARGUMENTS:
		printf("RSS_BAD_ARGUMENTS\n");
		break;
		
	default:
		printf("STATUS: %x\n", s);
	}
}


static void get_system_call_id(ApiInfoElementType * InfoElement, rsuint16 InfoElementLength) {

	ApiInfoElementType * info;
	ApiSystemCallIdType * callid;
	
	info = ApiGetInfoElement(InfoElement, InfoElementLength, API_IE_SYSTEM_CALL_ID);
	if ( info && info->IeLength > 0 ) {
		printf("API_IE_SYSTEM_CALL_ID\n");
		callid = (ApiSystemCallIdType *) &info->IeData[0];
			
		switch ( callid->ApiSubId ) {

		case API_SUB_CALL_ID:
			printf("API_SUB_CALL_ID\n");
			printf("ApiSystemCallId: %d\n", callid->ApiSystemCallId);
			break;

		case API_SUB_CALL_ID_UPDATE:
			printf("API_SUB_CALL_ID_UPDATE\n");
			break;

		case API_SUB_CALL_ID_INVALID:
			printf("API_SUB_CALL_ID_INVALID\n");
			break;
		}

	}
}



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
	
	ApiFpCcConnectIndType * p = (ApiFpCcConnectIndType *) &m->mail_header;
	ApiInfoElementType * ie_block = NULL;
	rsuint16 ie_block_len = 0;

	printf("handset: %d\n", p->CallReference.Instance.Fp);

	
	ApiBuildInfoElement(&ie_block,
			    &ie_block_len,
			    API_IE_CODEC_LIST,
			    NarrowBandCodecIe->IeLength,
			    (rsuint8 *) NarrowBandCodecIe->IeData);

	ApiBuildInfoElement(&ie_block,
			    &ie_block_len,
			    API_IE_SYSTEM_CALL_ID,
			    sizeof(ApiSystemCallIdType),
			    (rsuint8 *) &internal_call);

	
	ApiFpCcConnectReqType * req = (ApiFpCcConnectReqType*) malloc((sizeof(ApiFpCcConnectReqType) - 1 + ie_block_len));

        req->Primitive = API_FP_CC_CONNECT_REQ;
	req->CallReference = incoming_call;
        req->InfoElementLength = ie_block_len;
        memcpy(req->InfoElement,(rsuint8*)ie_block, ie_block_len);
	
	printf("API_FP_CC_CONNECT_REQ\n");
	busmail_send(dect_bus, (uint8_t *) req, sizeof(ApiFpCcConnectReqType) - 1 + ie_block_len);
	free(req);


}


static void alert_cfm(busmail_t *m) {

	ApiFpCcAlertCfmType * p = (ApiFpCcAlertCfmType *) &m->mail_header;

	printf("CallReference: %x\n", p->CallReference);
}


static void setup_ack_cfm(busmail_t *m) {

	ApiFpCcSetupAckCfmType * p = (ApiFpCcSetupAckCfmType *) &m->mail_header;

	printf("CallReference: %x\n", p->CallReference);
	print_status(p->Status);
}


static void alert_ind(busmail_t *m) {

	ApiFpCcAlertIndType * p = (ApiFpCcAlertIndType *) &m->mail_header;
	ApiFpCcAlertReqType * r;
	ApiInfoElementType * ie_block = NULL;
	rsuint16 ie_block_len = 0;
	ApiCallStatusType call_status;


	printf("CallReference: %x\n", p->CallReference);
	
	call_status.CallStatusSubId = API_SUB_CALL_STATUS;
	call_status.CallStatusValue.State = API_CSS_CALL_ALERTING;

	ApiBuildInfoElement(&ie_block,
			    &ie_block_len,
			    API_IE_CALL_STATUS,
			    sizeof(ApiCallStatusType),
			    (rsuint8 *) &call_status);

	ApiBuildInfoElement(&ie_block,
			    &ie_block_len,
			    API_IE_SYSTEM_CALL_ID,
			    sizeof(ApiSystemCallIdType),
			    (rsuint8 *) &internal_call);


	r = malloc(sizeof(ApiFpCcAlertReqType) - 1 + ie_block_len);	

        r->Primitive = API_FP_CC_ALERT_REQ;
	r->CallReference = incoming_call;
        r->InfoElementLength = ie_block_len;
        memcpy(r->InfoElement,(rsuint8*)ie_block, ie_block_len);
	
	printf("API_FP_CC_ALERT_REQ\n");
	busmail_send(dect_bus, (uint8_t *) r, sizeof(ApiFpCcAlertReqType) - 1 + ie_block_len);
	free(r);



	
}


static void connect_cfm(busmail_t *m) {
	
	ApiFpCcConnectCfmType * p = (ApiFpCcConnectCfmType *) &m->mail_header;

	if ( p->InfoElementLength > 0 ) {
		get_system_call_id( (ApiInfoElementType *) p->InfoElement, p->InfoElementLength);
	}

	ApiFpCcConnectResType res = {
		.Primitive = API_FP_CC_CONNECT_RES,
		.CallReference = incoming_call,
		.Status = RSS_SUCCESS,
		.InfoElementLength = 0,
	};
	



	printf("API_FP_CC_CONNECT_RES\n");
	busmail_send(dect_bus, (uint8_t *)&res, sizeof(res));

}



static void setup_cfm(busmail_t *m) {
	
	ApiFpCcSetupCfmType * p = (ApiFpCcSetupCfmType *) &m->mail_header;
	
	printf("SystemCallId: %x\n", p->SystemCallId);
	printf("Status: %x\n", p->Status);
	
	print_status(p->Status);

	/* if ( p->InfoElementLength > 0 ) { */
	/* 	get_system_call_id( (ApiInfoElementType *) p->InfoElement, p->InfoElementLength); */
	/* } */

	outgoing_call = p->CallReference;
	printf("outgoing_call: %x\n", p->CallReference);
}


static void release_ind(busmail_t *m) {
	
	ApiFpCcReleaseIndType * p = (ApiFpCcReleaseIndType *) &m->mail_header;

	printf("CallReference: %x\n", p->CallReference);
	printf("Reason: %x\n", p->Reason);
	
	ApiFpCcReleaseResType res = {
		.Primitive = API_FP_CC_RELEASE_RES,
		.CallReference = p->CallReference,
		.Status = RSS_SUCCESS,
		.InfoElementLength = 0,
	};
	
	printf("API_FP_CC_RELEASE_RES\n");
	busmail_send(dect_bus, (uint8_t *)&res, sizeof(res));
}



ApiInfoElementType *
ApiGetInfoElement(ApiInfoElementType *IeBlockPtr,
                                      rsuint16 IeBlockLength,
                                      ApiIeType Ie)
{
	/* Ie is in little endian inside the infoElement
	   list while all arguments to function are in bigEndian */
	ApiInfoElementType *pIe = NULL;
	rsuint16 targetIe = Ie;  

	while (NULL != (pIe = ApiGetNextInfoElement(IeBlockPtr, IeBlockLength, pIe))) {
		if (pIe->Ie == targetIe) {
			/* Return the pointer to the info element found */
			return pIe; 
		}
	}

	/* Return NULL to indicate that we did not
	   find an info element wirh the IE specified */
	return NULL; 
}


ApiInfoElementType* ApiGetNextInfoElement(ApiInfoElementType *IeBlockPtr,
                                          rsuint16 IeBlockLength,
                                          ApiInfoElementType *IePtr)
{
	ApiInfoElementType *pEnd = (ApiInfoElementType*)((rsuint8*)IeBlockPtr + IeBlockLength);

	if (IePtr == NULL) {
		// return the first info element
		IePtr = IeBlockPtr;
		
	} else {
		// calc the address of the next info element
		IePtr = (ApiInfoElementType*)((rsuint8*)IePtr + RSOFFSETOF(ApiInfoElementType, IeData) + IePtr->IeLength);
	}

	if (IePtr < pEnd) {
		
		return IePtr; // return the pointer to the next info element
	}
	return NULL; // return NULL to indicate that we have reached the end
}


void ApiBuildInfoElement(ApiInfoElementType **IeBlockPtr,
                         rsuint16 *IeBlockLengthPtr,
                         ApiIeType Ie,
                         rsuint8 IeLength,
                         rsuint8 *IeData)
{

	rsuint16 newLength = *IeBlockLengthPtr + RSOFFSETOF(ApiInfoElementType, IeData) + IeLength;

	/* Ie is in little endian inside the infoElement list while all arguments to function are in bigEndian */
	rsuint16 targetIe = Ie;
	//  RevertByteOrder( sizeof(ApiIeType),(rsuint8*)&targetIe   );          

	/* Allocate / reallocate a heap block to store (append) the info elemte in. */
	ApiInfoElementType *p = malloc(newLength);

	if (p == NULL) {

		// We failed to get e new block.
		// We free the old and return with *IeBlockPtr == NULL.
		ApiFreeInfoElement(IeBlockPtr);
		*IeBlockLengthPtr = 0;
	} else {
		// Update *IeBlockPointer with the address of the new block
		//     *IeBlockPtr = p;
		if( *IeBlockPtr != NULL ) {
		
			/* Copy over existing block data */
			memcpy( (rsuint8*)p, (rsuint8*)*IeBlockPtr, *IeBlockLengthPtr);
		
			/* Free existing block memory */
			ApiFreeInfoElement(IeBlockPtr);
		}
    
		/* Assign newly allocated block to old pointer */
		*IeBlockPtr = p;

		// Append the new info element to the allocated block
		p = (ApiInfoElementType*)(((rsuint8*)p) + *IeBlockLengthPtr); // p now points to the first byte of the new info element

		p->Ie = targetIe;

		p->IeLength = IeLength;
		memcpy (p->IeData, IeData, IeLength);
		// Update *IeBlockLengthPtr with the new block length
		*IeBlockLengthPtr = newLength;
	}

}




void ApiFreeInfoElement(ApiInfoElementType **IeBlockPtr)
{
	free((void*)*IeBlockPtr);

	*IeBlockPtr = NULL;
}


/* Caller dials */
static void setup_ind(busmail_t *m) {

	ApiFpCcSetupIndType * p = (ApiFpCcSetupIndType *) &m->mail_header;
	ApiFpCcAudioIdType Audio, OutAudio;
	ApiInfoElementType* info;
	ApiMultikeyPadType * keypad_entr = NULL;
        unsigned char keypad_len;
	ApiCallStatusType call_status;
	int i;
	ApiInfoElementType * ie_block = NULL;
	rsuint16 ie_block_len = 0;


	printf("CallReference: %x\n", p->CallReference);
	printf("TerminalIdInitiating: %d\n", p->TerminalId);
	printf("InfoElementLength: %d\n", p->InfoElementLength);
	
	incoming_call = p->CallReference;
	
	if ( p->InfoElementLength > 0 ) {
		get_system_call_id( (ApiInfoElementType *) p->InfoElement, p->InfoElementLength);
	}


	/* Reply to initiating handset */
	ApiFpCcSetupResType res = {
		.Primitive = API_FP_CC_SETUP_RES,
		.CallReference = incoming_call,
		.Status = RSS_SUCCESS,
		.AudioId.IntExtAudio = API_IEA_INT,
		.AudioId.SourceTerminalId = 2,
	};

	printf("API_FP_CC_SETUP_RES\n");
	busmail_send(dect_bus, (uint8_t *)&res, sizeof(ApiFpCcSetupResType));


	/* Call progress state to initiating handset */
	call_status.CallStatusSubId = API_SUB_CALL_STATUS;
	call_status.CallStatusValue.State = API_CSS_CALL_SETUP_ACK;
	
	ApiBuildInfoElement(&ie_block,
			    &ie_block_len,
			    API_IE_SYSTEM_CALL_ID,
			    sizeof(ApiSystemCallIdType),
			    (rsuint8 *) &internal_call);

	ApiBuildInfoElement(&ie_block,
			    &ie_block_len,
			    API_IE_CALL_STATUS,
			    sizeof(ApiCallStatusType),
			    (rsuint8 *) &call_status);


	ApiFpCcSetupAckReqType * ra = (ApiFpCcSetupAckReqType *) malloc(sizeof(ApiFpCcSetupAckReqType) - 1 + ie_block_len);
	ra->Primitive = API_FP_CC_SETUP_ACK_REQ;
	ra->CallReference = incoming_call;
	ra->Signal = API_CC_SIGNAL_TONES_OFF;
	ra->ProgressInd = API_IN_BAND_NOT_AVAILABLE;
	
	memcpy(ra->InfoElement, ie_block, ie_block_len);

	printf("API_FP_CC_SETUP_ACK_REQ\n");
	busmail_send(dect_bus, (uint8_t *)ra, sizeof(ApiFpCcSetupAckReqType) - 1 + ie_block_len);
	free(ra);


	/* Connection request to dialed handset */
	ie_block = NULL;
	ie_block_len = 0;

	call_status.CallStatusSubId = API_SUB_CALL_STATUS;
	call_status.CallStatusValue.State = API_CSS_CALL_SETUP;
	
	ApiBuildInfoElement(&ie_block,
			    &ie_block_len,
			    API_IE_SYSTEM_CALL_ID,
			    sizeof(ApiSystemCallIdType),
			    (rsuint8 *) &internal_call);

	ApiBuildInfoElement(&ie_block,
			    &ie_block_len,
			    API_IE_CALL_STATUS,
			    sizeof(ApiCallStatusType),
			    (rsuint8 *) &call_status);


	ApiFpCcSetupReqType * req = (ApiFpCcSetupReqType *) malloc(sizeof(ApiFpCcSetupReqType) - 1 + ie_block_len);

	req->Primitive = API_FP_CC_SETUP_REQ;
	req->TerminalId = 2;
	req->AudioId.SourceTerminalId = 1;
	req->AudioId.IntExtAudio = API_IEA_INT;
	req->BasicService = API_BASIC_SPEECH;
	req->CallClass = API_CC_NORMAL;
	req->Signal = API_CC_SIGNAL_ALERT_ON_PATTERN_2;
	req->InfoElementLength = ie_block_len;

	memcpy(req->InfoElement, ie_block, ie_block_len);

	printf("API_FP_CC_SETUP_REQ\n");
	busmail_send(dect_bus, (uint8_t *)req, sizeof(ApiFpCcSetupReqType) - 1 + ie_block_len);
	free(req);

	return;
}


static void pinging_call(int handset) {

	/* Connection request to dialed handset */
	ApiFpCcSetupReqType req = {
		.Primitive = API_FP_CC_SETUP_REQ,
		.TerminalId = handset,
		.AudioId.SourceTerminalId = 0, /* 0 is the base station id */
		.BasicService = API_BASIC_SPEECH,
		.CallClass = API_CC_NORMAL,
		.Signal = API_CC_SIGNAL_ALERT_ON_PATTERN_2,
		.InfoElementLength = 0,
	};

	printf("pinging_call\n");
	printf("API_FP_CC_SETUP_REQ\n");
	busmail_send(dect_bus, (uint8_t *)&req, sizeof(ApiFpCcSetupReqType));
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


static void application_frame(packet_t *p) {
	
	int i;
	busmail_t * m = (busmail_t *) &p->data[0];
	
	switch (m->task_id) {

	case 0:

		/* Production test command */
		if ( client_connected == 1 ) {

			/* Send packets to connected clients */
			printf("send to client_bus\n");
			packet_dump(p);
			eap_send(client_bus, &p->data[3], p->size - 3);
		}
		break;

	case 1:

		/* Application command */
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

		case API_FP_CC_SETUP_IND:
			printf("API_FP_CC_SETUP_IND\n");
			setup_ind(m);
			break;

		case API_FP_CC_SETUP_REQ:
			printf("API_FP_CC_SETUP_REQ\n");
			break;

		case API_FP_CC_RELEASE_IND:
			printf("API_FP_CC_RELEASE_IND\n");
			release_ind(m);
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

		case API_FP_CC_CONNECT_CFM:
			printf("API_FP_CC_CONNECT_CFM\n");
			connect_cfm(m);
			break;

		case API_FP_CC_ALERT_IND:
			printf("API_FP_CC_ALERT_IND\n");
			alert_ind(m);
			break;

		case API_FP_CC_ALERT_CFM:
			printf("API_FP_CC_ALERT_CFM\n");
			alert_cfm(m);
			break;

		case API_FP_CC_SETUP_ACK_CFM:
			printf("API_FP_CC_SETUP_ACK_CFM\n");
			setup_ack_cfm(m);
			break;

		}
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
