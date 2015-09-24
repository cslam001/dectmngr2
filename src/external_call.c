#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <Api/FpGeneral/ApiFpGeneral.h>
#include <Api/CodecList/ApiCodecList.h>
#include <Api/FpCc/ApiFpCc.h>
#include <Api/FpMm/ApiFpMm.h>
#include <Api/ProdTest/ApiProdTest.h>
#include <Api/FpAudio/ApiFpAudio.h>
#include <Api/RsStandard.h>

#include "busmail.h"
#include "natalie_utils.h"

/* Module scope variables */
static ApiCallReferenceType incoming_call;
static ApiCallReferenceType outgoing_call;
static ApiSystemCallIdType * external_call;
static ApiSystemCallIdType * system_call_id;
static ApiLineIdValueType * outgoing_line_id;
static char nbwbCodecList[NBWB_CODECLIST_LENGTH]={0x01, 0x02, 0x03, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00, 0x04};
static char nbCodecList[]={0x01, 0x01, 0x02, 0x00, 0x00, 0x04};
static char wbCodecList[]={0x01, 0x01, 0x03, 0x00, 0x00, 0x01};
static rsuint8 NarrowCodecArr[30];
static rsuint8 WideCodecArr[30];
static ApiInfoElementType * NarrowBandCodecIe = (ApiInfoElementType*) NarrowCodecArr;
static const rsuint16 NarrowBandCodecIeLen = (RSOFFSETOF(ApiInfoElementType, IeData) + 6);
static ApiInfoElementType * WideBandCodecIe = (ApiInfoElementType*) WideCodecArr;
static const rsuint16 WideBandCodecIeLen = (RSOFFSETOF(ApiInfoElementType, IeData) + 6);
static ApiCodecListType * codecs = NULL;
static void * dect_bus;




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
}


static ApiCodecListType * get_dialed_nr(ApiInfoElementType * InfoElement, rsuint16 InfoElementLength) {

	ApiInfoElementType * info;
	ApiMultikeyPadType * keypad = NULL;
	int keypad_len, i;
	char * keypad_str;
	
	info = ApiGetInfoElement(InfoElement, InfoElementLength, API_IE_MULTIKEYPAD);
	if ( info && info->IeLength > 0 ) {
		printf("API_IE_MULTIKEYPAD\n");
		keypad = (ApiCodecListType *) &info->IeData[0];
		keypad_len = info->IeLength;

		keypad_str = (char *) malloc(keypad_len + 1);
		memcpy(keypad_str, keypad, keypad_len);
		keypad_str[keypad_len] = '\0';

		printf("keypad: %s\n", keypad_str);

		return keypad_str;
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


static ApiLineIdValueType * get_line_id(ApiInfoElementType * InfoElement, rsuint16 InfoElementLength) {

	ApiInfoElementType * info;
	ApiCodecListType * list;
	ApiLineIdValueType * line_id;
	
	info = ApiGetInfoElement(InfoElement, InfoElementLength, API_IE_LINE_ID);
	if ( info && info->IeLength > 0 ) {
		printf("\n\nGotLineId\n\n");
		line_id = malloc(sizeof(ApiLineIdValueType));
		memcpy(line_id, &info->IeData[0], info->IeLength);

		return line_id;
	}
	printf("\n\nNo LineId\n\n");
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
	printf("internal_call: %x\n", system_call_id->ApiSystemCallId);

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
			    (rsuint8 *) system_call_id);

	
	ApiFpCcConnectReqType * req = (ApiFpCcConnectReqType*) malloc((sizeof(ApiFpCcConnectReqType) - 1 + ie_block_len));

        req->Primitive = API_FP_CC_CONNECT_REQ;
	req->CallReference = incoming_call;
        req->InfoElementLength = ie_block_len;
        memcpy(req->InfoElement,(rsuint8*)ie_block, ie_block_len);

	codecs = get_codecs((ApiInfoElementType *) req->InfoElement, req->InfoElementLength);

	printf("API_FP_CC_CONNECT_REQ\n");
	busmail_send(dect_bus, (uint8_t *) req, sizeof(ApiFpCcConnectReqType) - 1 + ie_block_len);
	free(req);


}


static void reject_ind(busmail_t *m) {

	ApiFpCcRejectIndType * p = (ApiFpCcRejectIndType *) &m->mail_header;

	printf("CallReference: %x\n", p->CallReference);
	printf("Reason: %x\n", p->Reason);
}



static void alert_cfm(busmail_t *m) {

	ApiFpCcAlertCfmType * p = (ApiFpCcAlertCfmType *) &m->mail_header;
	rsuint16 ie_block_len = 0;
	ApiInfoElementType * ie_block = NULL;
	ApiCallStatusType call_status;

	printf("CallReference: %x\n", p->CallReference);
	print_status(p->Status);

	/* Connect handset */
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
			    NarrowBandCodecIe->IeLength,
			    (rsuint8 *) NarrowBandCodecIe->IeData);
	
	ApiBuildInfoElement(&ie_block,
			    &ie_block_len,
			    API_IE_SYSTEM_CALL_ID,
			    sizeof(ApiSystemCallIdType),
			    (rsuint8 *) system_call_id);


	ApiFpCcConnectReqType * req = (ApiFpCcConnectReqType*) malloc((sizeof(ApiFpCcConnectReqType) - 1 + ie_block_len));
	req->Primitive = API_FP_CC_CONNECT_REQ;
	req->CallReference = incoming_call;
	req->InfoElementLength = ie_block_len;
	memcpy(req->InfoElement,(rsuint8*)ie_block, ie_block_len);

	printf("API_FP_CC_CONNECT_REQ\n");
	busmail_send(dect_bus, (uint8_t *) req, sizeof(ApiFpCcConnectReqType) - 1 + ie_block_len);
	free(req);

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
	



	
}


static void connect_cfm(busmail_t *m) {
	
	ApiFpCcConnectCfmType * p = (ApiFpCcConnectCfmType *) &m->mail_header;
	ApiInfoElementType * ie_block = NULL;
	rsuint16 ie_block_len = 0;
	ApiCallStatusType call_status;

	printf("CallReference: %x\n", p->CallReference);
	print_status(p->Status);
}


static void info_ind(busmail_t *m) {

	ApiFpCcInfoIndType * p = (ApiFpCcInfoIndType *) &m->mail_header;
	rsuint16 ie_block_len = 0;
	ApiInfoElementType * ie_block = NULL;
	ApiCallStatusType call_status;
	const char json[15];
	char * dialed_nr;

	printf("CallReference: %x\n", p->CallReference);

	if ( p->InfoElementLength > 0 ) {
		dialed_nr = get_dialed_nr((ApiInfoElementType *) p->InfoElement, p->InfoElementLength);
	}

	if (dialed_nr) {

	
	snprintf(json, 50, "{ \"terminal\": \"%d\", \"dialed_nr\": \"%s\" }", p->CallReference.Instance.Host, dialed_nr);
	ubus_send_json_string("dect.api.info_ind", json);

	} else {

		ie_block_len = 0;
		ie_block = NULL;

		/* Connect handset */
		call_status.CallStatusSubId = API_SUB_CALL_STATUS;
		call_status.CallStatusValue.State = API_CSS_CALL_PROC;

		ApiBuildInfoElement(&ie_block,
				    &ie_block_len,
				    API_IE_CALL_STATUS,
				    sizeof(ApiCallStatusType),
				    (rsuint8 *) &call_status);
	
		ApiBuildInfoElement(&ie_block,
				    &ie_block_len,
				    API_IE_SYSTEM_CALL_ID,
				    sizeof(ApiSystemCallIdType),
				    (rsuint8 *) system_call_id);


		ApiFpCcCallProcReqType * req = (ApiFpCcCallProcReqType*) malloc((sizeof(ApiFpCcCallProcReqType) - 1 + ie_block_len));
		req->Primitive = API_FP_CC_CALL_PROC_REQ;
		req->CallReference = incoming_call;
		req->ProgressInd = API_IN_BAND_NOT_AVAILABLE;
		req->InfoElementLength = ie_block_len;
		memcpy(req->InfoElement,(rsuint8*)ie_block, ie_block_len);

		printf("API_FP_CC_CALL_PROC_REQ\n");
		busmail_send(dect_bus, (uint8_t *) req, sizeof(ApiFpCcCallProcReqType) - 1 + ie_block_len);
		free(req);
	}
}


static void call_proc_cfm(busmail_t *m) {


	ApiFpCcCallProcCfmType * p = (ApiFpCcCallProcCfmType *) &m->mail_header;
	rsuint16 ie_block_len = 0;
	ApiInfoElementType * ie_block = NULL;
	ApiCallStatusType call_status;
	
	print_status(p->Status);

	
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
			    (rsuint8 *) system_call_id);


	ApiFpCcAlertReqType * r = malloc(sizeof(ApiFpCcAlertReqType) - 1 + ie_block_len);

        r->Primitive = API_FP_CC_ALERT_REQ;
	r->CallReference = incoming_call;
        r->InfoElementLength = ie_block_len;
        memcpy(r->InfoElement,(rsuint8*)ie_block, ie_block_len);
	
	printf("API_FP_CC_ALERT_REQ\n");
	busmail_send(dect_bus, (uint8_t *) r, sizeof(ApiFpCcAlertReqType) - 1 + ie_block_len);
	free(r);


}


static void audio_format_cfm(busmail_t *m) {

	ApiFpSetAudioFormatCfmType * p = (ApiFpSetAudioFormatCfmType *) &m->mail_header;
	rsuint16 ie_block_len = 0;
	ApiInfoElementType * ie_block = NULL;
	ApiCallStatusType call_status;
	
	print_status(p->Status);

	/* Call progress state to initiating handset */
	call_status.CallStatusSubId = API_SUB_CALL_STATUS;
	call_status.CallStatusValue.State = API_CSS_CALL_SETUP_ACK;
	
	ApiBuildInfoElement(&ie_block,
			    &ie_block_len,
			    API_IE_SYSTEM_CALL_ID,
			    sizeof(ApiSystemCallIdType),
			    (rsuint8 *) system_call_id);

	ApiBuildInfoElement(&ie_block,
			    &ie_block_len,
			    API_IE_CALL_STATUS,
			    sizeof(ApiCallStatusType),
			    (rsuint8 *) &call_status);


	ApiFpCcSetupAckReqType * ra = (ApiFpCcSetupAckReqType *) malloc(sizeof(ApiFpCcSetupAckReqType) - 1 + ie_block_len);
	ra->Primitive = API_FP_CC_SETUP_ACK_REQ;
	ra->CallReference = incoming_call;
	ra->Signal = API_CC_SIGNAL_DIAL_TONE_ON;
	ra->ProgressInd = API_IN_BAND_NOT_AVAILABLE;

	ra->InfoElementLength = ie_block_len;
	memcpy(ra->InfoElement, ie_block, ie_block_len);
	
	printf("API_FP_CC_SETUP_ACK_REQ\n");
	
	busmail_send(dect_bus, (uint8_t *)ra, sizeof(ApiFpCcSetupAckReqType) - 1 + ie_block_len);
	free(ra);

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
	const char term[15];

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

	snprintf(term, 15, "%d", p->CallReference.Instance.Host);

	ubus_send_string_api("dect.api.release_ind", "terminal", term);
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
	ApiAudioEndPointIdType endpt;
	const char term[15];
	ApiLineIdValueType * line_id;

	printf("CallReference: %x\n", p->CallReference);
	printf("TerminalIdInitiating: %d\n", p->TerminalId);
	printf("InfoElementLength: %d\n", p->InfoElementLength);
	printf("BasicService: %d\n", p->BasicService);
	printf("CallClass: %d\n", p->CallClass);
	
	incoming_call = p->CallReference;
	incoming_call.Instance.Host = p->TerminalId;

	snprintf(term, 15, "%d", p->TerminalId);
	ubus_send_string_api("dect.api.setup_ind", "terminal", term);

	endpt = p->TerminalId - 1;
	
	if ( p->InfoElementLength > 0 ) {
		system_call_id = get_system_call_id( (ApiInfoElementType *) p->InfoElement, p->InfoElementLength);
		printf("internal_call: %x\n", system_call_id->ApiSystemCallId);

		codecs = get_codecs((ApiInfoElementType *) p->InfoElement, p->InfoElementLength);
		
		line_id = get_line_id((ApiInfoElementType *) p->InfoElement, p->InfoElementLength);
	}
		

	/* Reply to initiating handset */
	ApiFpCcSetupResType res = {
		.Primitive = API_FP_CC_SETUP_RES,
		.CallReference = incoming_call,
		.Status = RSS_SUCCESS,
		.AudioId.IntExtAudio = API_IEA_EXT,
		.AudioId.AudioEndPointId = endpt,
	};

	printf("API_FP_CC_SETUP_RES\n");
	busmail_send(dect_bus, (uint8_t *)&res, sizeof(ApiFpCcSetupResType));


	ApiFpSetAudioFormatReqType  aud_req = {
		.Primitive = API_FP_SET_AUDIO_FORMAT_REQ,
		.DestinationId = endpt,
		.AudioDataFormat = AP_DATA_FORMAT_LINEAR_16kHz
	};

	printf("API_FP_SET_AUDIO_FORMAT_REQ\n");
	busmail_send(dect_bus, (uint8_t *)&aud_req, sizeof(ApiFpSetAudioFormatReqType));
	
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





void external_call_handler(packet_t *p) {
	
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

	case API_FP_SET_AUDIO_FORMAT_CFM:
		audio_format_cfm(m);
		break;

	case API_FP_CC_CALL_PROC_CFM:
		call_proc_cfm(m);
		break;

	}
}


void external_call_init(void * bus) {
	
	dect_bus = bus;

	dect_codec_init();
	busmail_add_handler(bus, external_call_handler);
}
