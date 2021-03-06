#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <Api/FpGeneral/ApiFpGeneral.h>
#include <Api/CodecList/ApiCodecList.h>
#include <Api/FpCc/ApiFpCc.h>
#include <Api/FpMm/ApiFpMm.h>
#include <Api/ProdTest/ApiProdTest.h>
#include <Api/RsStandard.h>

#include "busmail.h"
#include "util.h"
#include "natalie_utils.h"


ApiCallReferenceType incoming_call;
ApiCallReferenceType outgoing_call;
ApiSystemCallIdType * internal_call;

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
ApiInfoElementType * WideBandCodecIe = (ApiInfoElementType*) WideCodecArr;
const rsuint16 WideBandCodecIeLen = (RSOFFSETOF(ApiInfoElementType, IeData) + 6);

ApiCodecListType * codecs = NULL;

static void *dect_bus;


static ApiSystemCallIdType * get_system_call_id(ApiInfoElementType * InfoElement, rsuint16 InfoElementLength) {

	ApiInfoElementType * info;
	ApiSystemCallIdType * callid;
	ApiSystemCallIdType * id;
	
	info = ApiGetInfoElement(InfoElement, InfoElementLength, API_IE_SYSTEM_CALL_ID);
	if ( info && info->IeLength > 0 ) {
		printf("API_IE_SYSTEM_CALL_ID\n");
		callid = (ApiSystemCallIdType *) &info->IeData[0];
			
		switch ( callid->ApiSubId ) {

		case API_SUB_CALL_ID:
			printf("API_SUB_CALL_ID\n");
			id = malloc(sizeof(ApiSystemCallIdType));
			memcpy(id, callid, sizeof(ApiSystemCallIdType));
			printf("callid: %x\n", id->ApiSystemCallId);
			return id;
			break;

		case API_SUB_CALL_ID_UPDATE:
			printf("API_SUB_CALL_ID_UPDATE\n");
			return NULL;
			break;

		case API_SUB_CALL_ID_INVALID:
			printf("API_SUB_CALL_ID_INVALID\n");
			return NULL;
			break;
		}

	}

	return NULL;
}


static ApiCodecListType * get_codecs(ApiInfoElementType * InfoElement, rsuint16 InfoElementLength) {

	ApiInfoElementType * info;
	ApiCodecListType * list;
	
	info = ApiGetInfoElement(InfoElement, InfoElementLength, API_IE_CODEC_LIST);
	if ( info && info->IeLength > 0 ) {
		printf("API_IE_CODEC_LIST\n");
		list = (ApiCodecListType *) &info->IeData[0];
		printf("NegotiationIndicator: %x\n", list->NegotiationIndicator);
		printf("NoOfCodecs: %x\n", list->NoOfCodecs);

		list = malloc(info->IeLength);
		memcpy(list, &info->IeData[0], info->IeLength);

		return list;
	}

	return NULL;
}



static void dect_codec_init(void)
{
	NarrowBandCodecIe->Ie = API_IE_CODEC_LIST;
	NarrowBandCodecIe->IeLength = 6;
	NarrowBandCodecIe->IeData[0] = 0x01; // NegotiationIndicator, Negotiation possible
	NarrowBandCodecIe->IeData[1] = 0x01; // NoOfCodecs
	NarrowBandCodecIe->IeData[2] = 0x02; // API_CT_G726 API_MDS_1_MD
	NarrowBandCodecIe->IeData[3] = 0x00;  // MacDlcService
	NarrowBandCodecIe->IeData[4] = 0x00;  // CplaneRouting  API_CPR_CS
	NarrowBandCodecIe->IeData[5] = 0x04;  // SlotSize API_SS_FS fullslot

	WideBandCodecIe->Ie = API_IE_CODEC_LIST;
	WideBandCodecIe->IeLength  = 6;
	WideBandCodecIe->IeData[0] = 0x01;
	WideBandCodecIe->IeData[1] = 0x01;
	WideBandCodecIe->IeData[2] = 0x03;
	WideBandCodecIe->IeData[3] = 0x00;
	WideBandCodecIe->IeData[4] = 0x00;
	WideBandCodecIe->IeData[5] = 0x01;

	return;
}



/* Handset answers */
static void connect_ind(busmail_t *m) {
	
	ApiFpCcConnectIndType * p = (ApiFpCcConnectIndType *) &m->mail_header;
	ApiInfoElementType * ie_block = NULL;
	rsuint16 ie_block_len = 0;
	ApiCallStatusType call_status;

	printf("CallReference: %x\n", p->CallReference);
	printf("p->InfoElementLength: %d\n", p->InfoElementLength);
	printf("internal_call: %x\n", internal_call->ApiSystemCallId);

	if (p->InfoElementLength > 0) {
		codecs = get_codecs((ApiInfoElementType *) p->InfoElement, p->InfoElementLength);
	}
	
	/* Call progress state to initiating handset */
	call_status.CallStatusSubId = API_SUB_CALL_STATUS;
	call_status.CallStatusValue.State = API_CSS_CALL_CONNECT;

	ApiBuildInfoElement(&ie_block,
			    &ie_block_len,
			    API_IE_CALL_STATUS,
			    sizeof(ApiCallStatusType),
			    (rsuint8 *) &call_status);

	ApiBuildInfoElement(&ie_block,
			    &ie_block_len,
			    API_IE_CODEC_LIST,
			    WideBandCodecIe->IeLength,
			    (rsuint8 *) WideBandCodecIe->IeData);

	ApiBuildInfoElement(&ie_block,
			    &ie_block_len,
			    API_IE_SYSTEM_CALL_ID,
			    sizeof(ApiSystemCallIdType),
			    (rsuint8 *) internal_call);

	
	ApiFpCcConnectReqType * req = (ApiFpCcConnectReqType*) malloc((sizeof(ApiFpCcConnectReqType) - 1 + ie_block_len));

        req->Primitive = API_FP_CC_CONNECT_REQ;
	req->CallReference = incoming_call;
        req->InfoElementLength = ie_block_len;
        memcpy(req->InfoElement,(rsuint8*)ie_block, ie_block_len);

	codecs = get_codecs((ApiInfoElementType *) req->InfoElement, req->InfoElementLength);

	printf("API_FP_CC_CONNECT_REQ\n");
	mailProto.send(dect_bus, (uint8_t *) req, sizeof(ApiFpCcConnectReqType) - 1 + ie_block_len);
	free(req);


}


static void reject_ind(busmail_t *m) {

	ApiFpCcRejectIndType * p = (ApiFpCcRejectIndType *) &m->mail_header;

	printf("CallReference: %x\n", p->CallReference);
	printf("Reason: %x\n", p->Reason);
}



static void alert_cfm(busmail_t *m) {

	ApiFpCcAlertCfmType * p = (ApiFpCcAlertCfmType *) &m->mail_header;

	printf("CallReference: %x\n", p->CallReference);
	print_status(p->Status);
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
	printf("p->InfoElementLength: %d\n", p->InfoElementLength);

	if (p->InfoElementLength > 0) {
		codecs = get_codecs((ApiInfoElementType *) p->InfoElement, p->InfoElementLength);
	}
	
	/* call_status.CallStatusSubId = API_SUB_CALL_STATUS; */
	/* call_status.CallStatusValue.State = API_CSS_CALL_ALERTING; */

	/* ApiBuildInfoElement(&ie_block, */
	/* 		    &ie_block_len, */
	/* 		    API_IE_CALL_STATUS, */
	/* 		    sizeof(ApiCallStatusType), */
	/* 		    (rsuint8 *) &call_status); */

	/* ApiBuildInfoElement(&ie_block, */
	/* 		    &ie_block_len, */
	/* 		    API_IE_SYSTEM_CALL_ID, */
	/* 		    sizeof(ApiSystemCallIdType), */
	/* 		    (rsuint8 *) internal_call); */


	/* r = malloc(sizeof(ApiFpCcAlertReqType) - 1 + ie_block_len);	 */

        /* r->Primitive = API_FP_CC_ALERT_REQ; */
	/* r->CallReference = incoming_call; */
        /* r->InfoElementLength = ie_block_len; */
        /* memcpy(r->InfoElement,(rsuint8*)ie_block, ie_block_len); */
	
	/* printf("API_FP_CC_ALERT_REQ\n"); */
	/* busmail_send(dect_bus, (uint8_t *) r, sizeof(ApiFpCcAlertReqType) - 1 + ie_block_len); */
	/* free(r); */



	
}


static void connect_cfm(busmail_t *m) {
	
	ApiFpCcConnectCfmType * p = (ApiFpCcConnectCfmType *) &m->mail_header;
	ApiInfoElementType * ie_block = NULL;
	rsuint16 ie_block_len = 0;
	ApiCallStatusType call_status;

	printf("CallReference: %x\n", p->CallReference);
	print_status(p->Status);

	if ( p->InfoElementLength > 0 ) {
		get_system_call_id( (ApiInfoElementType *) p->InfoElement, p->InfoElementLength);
	}

	ApiFpCcConnectResType res = {
		.Primitive = API_FP_CC_CONNECT_RES,
		.CallReference = outgoing_call,
		.Status = RSS_SUCCESS,
		.InfoElementLength = 0,
	};
	
	
	printf("API_FP_CC_CONNECT_RES\n");
	mailProto.send(dect_bus, (uint8_t *)&res, sizeof(res));

	/* /\* Call progress state to initiating handset *\/ */
	/* call_status.CallStatusSubId = API_SUB_CALL_STATUS; */
	/* call_status.CallStatusValue.State = API_CSS_CALL_CONNECT; */
	
	/* ApiBuildInfoElement(&ie_block, */
	/* 		    &ie_block_len, */
	/* 		    API_IE_SYSTEM_CALL_ID, */
	/* 		    sizeof(ApiSystemCallIdType), */
	/* 		    (rsuint8 *) internal_call); */

	/* ApiBuildInfoElement(&ie_block, */
	/* 		    &ie_block_len, */
	/* 		    API_IE_CALL_STATUS, */
	/* 		    sizeof(ApiCallStatusType), */
	/* 		    (rsuint8 *) &call_status); */

	/* ApiFpCcInfoReqType * r = (ApiFpCcInfoReqType *) malloc(sizeof(ApiFpCcInfoReqType) - 1 + ie_block_len); */
	/* r->Primitive = API_FP_CC_INFO_REQ; */
	/* r->CallReference = outgoing_call; */
	
	/* memcpy(r->InfoElement, ie_block, ie_block_len); */

	/* printf("API_FP_CC_INFO_REQ\n"); */
	/* busmail_send(dect_bus, (uint8_t *)r, sizeof(ApiFpCcSetupAckReqType) - 1 + ie_block_len); */
	/* free(r); */

}


static void info_ind(busmail_t *m) {

	ApiFpCcInfoIndType * p = (ApiFpCcInfoIndType *) &m->mail_header;
	rsuint16 ie_block_len = 0;
	ApiInfoElementType * ie_block = NULL;
	ApiCallStatusType call_status;

	printf("CallReference: %x\n", p->CallReference);

}

static void setup_cfm(busmail_t *m) {
	
	ApiFpCcSetupCfmType * p = (ApiFpCcSetupCfmType *) &m->mail_header;
	
	printf("SystemCallId: %x\n", p->SystemCallId);
	
	print_status(p->Status);

	outgoing_call = p->CallReference;
	printf("outgoing_call: %x\n", p->CallReference);
}



static void release_cfm(busmail_t *m) {
	
	ApiFpCcReleaseCfmType * p = (ApiFpCcReleaseCfmType *) &m->mail_header;

	printf("CallReference: %x\n", p->CallReference);	
	print_status(p->Status);
}


static void release_ind(busmail_t *m) {
	
	ApiFpCcReleaseIndType * p = (ApiFpCcReleaseIndType *) &m->mail_header;
	ApiCallReferenceType terminate_call;

	printf("CallReference: %x\n", p->CallReference);
	printf("Reason: %x\n", p->Reason);
	
	if ( p->CallReference.Value == incoming_call.Value ) {
		terminate_call = outgoing_call;
	} else {
		terminate_call = incoming_call;
	}

	printf("CallReference: %x\n", p->CallReference);

	ApiFpCcReleaseResType res = {
		.Primitive = API_FP_CC_RELEASE_RES,
		.CallReference = p->CallReference,
		.Status = RSS_SUCCESS,
		.InfoElementLength = 0,
	};
	
	printf("API_FP_CC_RELEASE_RES\n");
	mailProto.send(dect_bus, (uint8_t *)&res, sizeof(res));


	ApiFpCcReleaseReqType req = {
		.Primitive = API_FP_CC_RELEASE_REQ,
		.CallReference = terminate_call,
		.Reason = API_RR_NORMAL,
		.InfoElementLength = 0,
	};
	
	printf("API_FP_CC_RELEASE_REQ\n");
	mailProto.send(dect_bus, (uint8_t *)&req, sizeof(req));

}


/* Caller dials */
static void setup_ind(busmail_t *m) {

	ApiFpCcSetupIndType * p = (ApiFpCcSetupIndType *) &m->mail_header;
	ApiFpCcAudioIdType Audio, OutAudio;
	ApiInfoElementType* info;
	ApiMultikeyPadType * keypad_entr = NULL;
        unsigned char keypad_len;
	ApiCallStatusType call_status;
	int i, called_hs, calling_hs;
	ApiInfoElementType * ie_block = NULL;
	rsuint16 ie_block_len = 0;


	printf("CallReference: %x\n", p->CallReference);
	printf("TerminalIdInitiating: %d\n", p->TerminalId);
	printf("InfoElementLength: %d\n", p->InfoElementLength);
	printf("BasicService: %d\n", p->BasicService);
	printf("CallClass: %d\n", p->CallClass);
	
	incoming_call = p->CallReference;
	
	if ( p->TerminalId == 1 ) {
		calling_hs = 1;
		called_hs = 2;
	} else {
		calling_hs = 2;
		called_hs = 1; 
	}
	
	if ( p->InfoElementLength > 0 ) {
		internal_call = get_system_call_id( (ApiInfoElementType *) p->InfoElement, p->InfoElementLength);
		printf("internal_call: %x\n", internal_call->ApiSystemCallId);

		codecs = get_codecs((ApiInfoElementType *) p->InfoElement, p->InfoElementLength);
	}

		

	/* Reply to initiating handset */
	ApiFpCcSetupResType res = {
		.Primitive = API_FP_CC_SETUP_RES,
		.CallReference = incoming_call,
		.Status = RSS_SUCCESS,
		.AudioId.IntExtAudio = API_IEA_INT,
		.AudioId.SourceTerminalId = called_hs,
	};

	printf("API_FP_CC_SETUP_RES\n");
	mailProto.send(dect_bus, (uint8_t *)&res, sizeof(ApiFpCcSetupResType));


	/* Call progress state to initiating handset */
	call_status.CallStatusSubId = API_SUB_CALL_STATUS;
	call_status.CallStatusValue.State = API_CSS_CALL_SETUP_ACK;
	
	ApiBuildInfoElement(&ie_block,
			    &ie_block_len,
			    API_IE_SYSTEM_CALL_ID,
			    sizeof(ApiSystemCallIdType),
			    (rsuint8 *) internal_call);

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
	mailProto.send(dect_bus, (uint8_t *)ra, sizeof(ApiFpCcSetupAckReqType) - 1 + ie_block_len);
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
			    (rsuint8 *) internal_call);

	ApiBuildInfoElement(&ie_block,
			    &ie_block_len,
			    API_IE_CALL_STATUS,
			    sizeof(ApiCallStatusType),
			    (rsuint8 *) &call_status);


	ApiFpCcSetupReqType * req = (ApiFpCcSetupReqType *) malloc(sizeof(ApiFpCcSetupReqType) - 1 + ie_block_len);

	req->Primitive = API_FP_CC_SETUP_REQ;
	req->TerminalId = called_hs;
	req->CallReference.Value = 0;
	req->AudioId.SourceTerminalId = calling_hs;
	req->AudioId.IntExtAudio = API_IEA_INT;
	req->BasicService = API_WIDEBAND_SPEECH;
	req->CallClass = API_CC_INTERNAL;
	req->Signal = API_CC_SIGNAL_ALERT_ON_PATTERN_2;
	req->InfoElementLength = ie_block_len;

	memcpy(req->InfoElement, ie_block, ie_block_len);

	printf("API_FP_CC_SETUP_REQ\n");
	mailProto.send(dect_bus, (uint8_t *)req, sizeof(ApiFpCcSetupReqType) - 1 + ie_block_len);
	free(req);

	printf("\n\n");
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
	mailProto.send(dect_bus, (uint8_t *)&req, sizeof(ApiFpCcSetupReqType));
	return;
}





void internal_call_handler(packet_t *p) {
	
	busmail_t * m = (busmail_t *) &p->data[0];

	if (m->task_id != 1) return;

	/* Application command */
	switch (m->mail_header) {

	case API_FP_CC_SETUP_IND:
		setup_ind(m);
		break;

	case API_FP_CC_RELEASE_IND:
		release_ind(m);
		break;

	case API_FP_CC_RELEASE_CFM:
		release_cfm(m);
		break;

	case API_FP_CC_SETUP_CFM:
		setup_cfm(m);
		break;

	case API_FP_CC_REJECT_IND:
		reject_ind(m);
		break;

	case API_FP_CC_CONNECT_IND:
		connect_ind(m);
		break;

	case API_FP_CC_CONNECT_CFM:
		connect_cfm(m);
		break;

	case API_FP_CC_ALERT_IND:
		alert_ind(m);
		break;

	case API_FP_CC_ALERT_CFM:
		alert_cfm(m);
		break;

	case API_FP_CC_SETUP_ACK_CFM:
		setup_ack_cfm(m);
		break;

	case API_FP_CC_INFO_IND:
		info_ind(m);
		break;
	}
}


void internal_call_init(void * bus) {
	
	dect_bus = bus;

	dect_codec_init();
	mailProto.add_handler(bus, internal_call_handler);
}
