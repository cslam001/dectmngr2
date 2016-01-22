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
#include "ubus.h"
#include "natalie_utils.h"


//-------------------------------------------------------------
#define MAX_CALLS	4

enum cc_states_t {												// CC states as defined in ETSI EN 300 175-5 
	F00_NULL,
	F01_INITIATED,
	F02_OVERLAP_SEND,
	F03_PROCEEDING,
	F04_DELIVERED,
	F06_PRESENT,
	F07_RECEIVED,
	F10_ACTIVE,
	F19_RELEASE_PEND,
};


struct call_t {
	enum cc_states_t state;
	int idx;

	/* "Call ID’s are assigned by the FP and are uniquely defined
	 * system-wide. This allows proper handling of asynchronous
	 * messages (ApiSystemCallIdType)." */
	rsuint8 SystemCallId;

	/* "Seen from the host the connections to the handsets
	 * are referenced by the handset number (ApiCallReferenceType)" */
	ApiCallReferenceType CallReference;
	ApiTerminalIdType TerminalId;

	// The audio endpoint PCM slot
	ApiAudioEndPointIdType epId;

	ApiCcBasicServiceType BasicService;
};



//-------------------------------------------------------------
static const char *cc_state_names[] = {							// Human readable CC states. Must match order of "enum cc_states_t"!
	"F00-null",
	"F01-initiated",
	"F02-overlap sending",
	"F03-call proceeding",
	"F04-call delivered",
	"F06-call present",
	"F07-call received",
	"F10-active",
	"F19-release pending"
};


static struct call_t calls[MAX_CALLS];
static void * dect_bus;


//-------------------------------------------------------------
static int audio_format_req(struct call_t *call);



//-------------------------------------------------------------
// Extract SystemCallId which FP should have assigned for us
static rsuint8 get_system_call_id(ApiInfoElementType * InfoElement, rsuint16 InfoElementLength) {
	ApiInfoElementType * info;
	ApiSystemCallIdType * callid;

	info = ApiGetInfoElement(InfoElement, InfoElementLength, API_IE_SYSTEM_CALL_ID);
	if (info && info->IeLength) {
		callid = (ApiSystemCallIdType *) &info->IeData[0];
		printf("API_IE_SYSTEM_CALL_ID %d\n", callid->ApiSystemCallId);

		switch (callid->ApiSubId) {
			case API_SUB_CALL_ID:
			case API_SUB_CALL_ID_UPDATE:
				return callid->ApiSystemCallId;
				break;
			
			default:
				break;
			}
	}

	return 0;
}


static char* get_dialed_nr(ApiInfoElementType * InfoElement, rsuint16 InfoElementLength) {
	ApiInfoElementType * info;
	ApiMultikeyPadType * keypad = NULL;
	int keypad_len, i;
	char * keypad_str;
	
	info = ApiGetInfoElement(InfoElement, InfoElementLength, API_IE_MULTIKEYPAD);
	if(!info) return NULL;
	if(!info->IeLength) return NULL;

	printf("API_IE_MULTIKEYPAD\n");
	keypad = (ApiCodecListType *) &info->IeData[0];
	keypad_len = info->IeLength;
	keypad_str = (char *) malloc(keypad_len + 1);
	memcpy(keypad_str, keypad, keypad_len);
	keypad_str[keypad_len] = '\0';
	printf("keypad: %s\n", keypad_str);

	return keypad_str;
}


#if 0
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
#endif


static ApiLineIdValueType * get_line_id(ApiInfoElementType * InfoElement, rsuint16 InfoElementLength) {
	ApiInfoElementType * info;
	ApiCodecListType * list;
	ApiLineIdValueType * line_id;
	
	info = ApiGetInfoElement(InfoElement, InfoElementLength, API_IE_LINE_ID);
	if ( info && info->IeLength > 0 ) {
		printf("GotLineId\n");
		line_id = malloc(sizeof(ApiLineIdValueType));
		memcpy(line_id, &info->IeData[0], info->IeLength);

		return line_id;
	}
	return NULL;
}



//-------------------------------------------------------------
static struct call_t* find_free_call_slot(void) {
	int i;

	for(i = 0; i < MAX_CALLS; i++) {
		if(calls[i].state == F00_NULL && !calls[i].SystemCallId && 
				!calls[i].CallReference.Value) {
			printf("[%llu] Allocating call slot %d\n", timeSinceStart(), i);
			return &calls[i];
		}
	}

	return NULL;
}


//-------------------------------------------------------------
static struct call_t* find_call_by_endpoint_id(ApiAudioEndPointIdType epId) {
	int i;

	for(i = 0; i < MAX_CALLS; i++) {
		if(calls[i].CallReference.Value && calls[i].epId == epId) {
			return &calls[i];
		}
	}

	return NULL;
}


//-------------------------------------------------------------
static struct call_t* find_call_by_sys_id(rsuint8 SystemCallId) {
	int i;

	for(i = 0; i < MAX_CALLS; i++) {
		if(calls[i].CallReference.Value &&
				calls[i].SystemCallId == SystemCallId) {
			return &calls[i];
		}
	}

	return NULL;
}


//-------------------------------------------------------------
static struct call_t* find_call_by_ref(ApiCallReferenceType *CallReference) {
	int i;

	for(i = 0; i < MAX_CALLS; i++) {		
		if(calls[i].CallReference.Instance.Fp &&
				calls[i].CallReference.Instance.Fp == CallReference->Instance.Fp) {
			return &calls[i];
		}
		else if(calls[i].CallReference.Instance.Host &&
				calls[i].CallReference.Instance.Host == CallReference->Instance.Host) {
			return &calls[i];
		}
	}

	printf("Error, no call matches ref %d\n", CallReference->Instance.Fp);
	return &calls[0];
}


//-------------------------------------------------------------
static void free_call_slot(struct call_t *call) {
	memset(call, 0, sizeof(struct call_t));
	call->state = F00_NULL;
}



//-------------------------------------------------------------
// Handle an outgoing call from handset to outer world.
// State F-01 call initiated (ETSI EN 300 175-5).
static int setup_ind(busmail_t *m) {
	ApiFpCcSetupIndType * p = (ApiFpCcSetupIndType *) &m->mail_header;
	ApiFpCcSetupResType reply;
	struct call_t *call;
	
	// First check prerequisites. Can the call switch to next state?
	reply.Primitive = API_FP_CC_SETUP_RES;
	reply.CallReference.Instance.Fp = p->CallReference.Instance.Fp;
	reply.CallReference.Instance.Host = p->TerminalId;
	reply.AudioId.IntExtAudio = API_IEA_EXT;
	reply.AudioId.AudioEndPointId = p->TerminalId - 1;

	/* Don't accept source terminal ID higher than can fit into
	 * the four bit field "reply.CallReference.Instance.Host" */
	if(p->TerminalId > 15) {
		printf("Error, invalid terminal ID\n");
		reply.Status = RSS_FAILED;
		mailProto.send(dect_bus, (uint8_t *)&reply, sizeof(ApiFpCcSetupResType));
		return -1;
	}

	// Did FP allocate a system call ID?
	if(!p->InfoElementLength || 
			!ApiGetInfoElement(p->InfoElement, p->InfoElementLength,
			API_IE_SYSTEM_CALL_ID)) {
		printf("Error, got no system call ID\n");
		reply.Status = RSS_MISSING_PARAMETER;
		mailProto.send(dect_bus, (uint8_t *)&reply, sizeof(ApiFpCcSetupResType));
		return -1;
	}

	// OK, we accept the request. Try to setup a new phone call.
	call = find_free_call_slot();
	if(!call) {
		printf("Error, no free call slot\n");
		reply.Status = RSS_NO_RESOURCE;
		mailProto.send(dect_bus, (uint8_t *)&reply, sizeof(ApiFpCcSetupResType));
		return -1;
	}

	printf("Call %d state change from %s to %s\n", call->idx,
		cc_state_names[call->state], cc_state_names[F01_INITIATED]);
	call->state = F01_INITIATED;
	call->CallReference = reply.CallReference;
	call->TerminalId = p->TerminalId;
	call->BasicService = p->BasicService;
	call->epId = reply.AudioId.AudioEndPointId;
	call->SystemCallId = get_system_call_id(
		(ApiInfoElementType *) p->InfoElement, p->InfoElementLength);
	reply.Status = RSS_SUCCESS;
	mailProto.send(dect_bus, (uint8_t *)&reply, sizeof(ApiFpCcSetupResType));

	// Request PCM audio format
	if(audio_format_req(call)) return -1;

	return 0;
}



//-------------------------------------------------------------
// Initiate an incomming external call from outer world
// going to a handset.
int setup_req(int termId, int pcmId) {
const char number[] = { '0', '1', '2', '3', '4' };
	ApiCallingNumberType callerNumber;
	ApiCallStatusType status;
	ApiCodecListType *codecs;
	ApiLineIdType line;
	ApiFpCcSetupReqType *msg;
	struct call_t *call;
	rsuint16 bufLen;
	rsuint8 *buf;

	call = find_free_call_slot();
	if(!call) return -1;
	call->CallReference.Instance.Fp = 0;
	call->CallReference.Instance.Host = termId;
	call->epId = pcmId;
	call->TerminalId = termId;
	call->BasicService = API_WIDEBAND_SPEECH;

	buf = NULL;
	bufLen = 0;
	// Send "call setup"
	status.CallStatusSubId = API_SUB_CALL_STATUS;
	status.CallStatusValue.State = API_CSS_CALL_SETUP;
	ApiBuildInfoElement(&buf, &bufLen, API_IE_CALL_STATUS,
		sizeof(status), (rsuint8*) &status);

	line.ApiSubId = API_SUB_LINE_ID_EXT_LINE_ID;
	line.ApiLineValue.Value = 1;
	ApiBuildInfoElement(&buf, &bufLen, API_IE_LINE_ID,
		sizeof(line), (rsuint8*) &line);

	// Send callers phone number
	callerNumber.NumberType = ANT_NATIONAL;
	callerNumber.Npi = ANPI_NATIONAL;
	callerNumber.PresentationInd = API_PRESENTATION_ALLOWED;
	callerNumber.ScreeningInd = API_USER_PROVIDED_NOT_SCREENED;
	callerNumber.NumberLength = sizeof(number);
	memcpy(callerNumber.Number, number, sizeof(number));
	ApiBuildInfoElement(&buf, &bufLen, API_IE_CALLING_PARTY_NUMBER,
		sizeof(callerNumber) + sizeof(number), (rsuint8*) &callerNumber);

	// Send list of codecs we support to handset
	codecs = malloc(sizeof(ApiCodecListType) + sizeof(ApiCodecInfoType));
	codecs->NegotiationIndicator = API_NI_POSSIBLE;
	codecs->NoOfCodecs = 2;
	codecs->Codec[0].Codec = API_CT_G722;
	codecs->Codec[0].MacDlcService = API_MDS_1_MD;
	codecs->Codec[0].CplaneRouting = API_CPR_CS;
	codecs->Codec[0].SlotSize = API_SS_LS640;
	codecs->Codec[1].Codec = API_CT_G726;
	codecs->Codec[1].MacDlcService = API_MDS_1_MD;
	codecs->Codec[1].CplaneRouting = API_CPR_CS;
	codecs->Codec[1].SlotSize = API_SS_FS;
	ApiBuildInfoElement(&buf, &bufLen, API_IE_CODEC_LIST,
		sizeof(ApiCodecListType) + sizeof(ApiCodecInfoType), (rsuint8 *) codecs);

	msg = calloc(1, sizeof(ApiFpCcSetupReqType) - 1 + bufLen);
	msg->Primitive = API_FP_CC_SETUP_REQ;
	msg->CallReference = call->CallReference;
	msg->TerminalId = call->TerminalId;
	msg->AudioId.IntExtAudio = API_IEA_EXT;
	msg->AudioId.AudioEndPointId = call->epId;
	msg->AudioId.SourceTerminalId = call->epId;
	msg->BasicService = call->BasicService;
	msg->CallClass = API_CC_NORMAL;
	msg->Signal = API_CC_SIGNAL_ALERT_ON_PATTERN_1;
	msg->InfoElementLength = bufLen;
	memcpy(msg->InfoElement, buf, bufLen);
	
	mailProto.send(dect_bus, (uint8_t*) msg,
		sizeof(ApiFpCcSetupReqType) - 1 + bufLen);

	free(codecs);
	free(buf);
	free(msg);

	return 0;
}



static void setup_cfm(busmail_t *m) {
	struct call_t *call;

	ApiFpCcSetupCfmType * p = (ApiFpCcSetupCfmType *) &m->mail_header;
	print_status(p->Status);
	printf("SystemCallId: %x\n", p->SystemCallId);	
	printf("callref: %x\n", p->CallReference.Value);
	printf("callclass: %x\n", p->CallClass);

	call = find_call_by_ref(&p->CallReference);
	printf("callref2: %x\n", call->CallReference.Value);
	call->SystemCallId = p->SystemCallId;
}



//-------------------------------------------------------------
// Tell FP what audio format PCMx should use.
// TODO: change endpoint parameters in
// build_dir/target-arm_v7-a_uClibc-0.9.33.2_eabi/bcmkernel/bcm963xx/shared/opensource/boardparms/bcm963xx/boardparms_voice.c
// to wideband, so we can pass 16kHz samplerate.
static int audio_format_req(struct call_t *call) {
	ApiFpSetAudioFormatReqType pcmFormat;

	switch(call->state) {
		case F01_INITIATED:
		case F06_PRESENT:
			pcmFormat.AudioDataFormat = AP_DATA_FORMAT_LINEAR_8kHz;
			break;

		default:
			pcmFormat.AudioDataFormat = AP_DATA_FORMAT_NONE;
			break;
	}			

	pcmFormat.Primitive = API_FP_SET_AUDIO_FORMAT_REQ;
	pcmFormat.DestinationId = call->epId;
	printf("API_FP_SET_AUDIO_FORMAT_REQ\n");
	mailProto.send(dect_bus, (uint8_t *) &pcmFormat, sizeof(pcmFormat));

	return 0;
}


//-------------------------------------------------------------
// Send a list of audio codecs we support to
// handset and play a dial tone.
static void audio_format_cfm(busmail_t *m) {
	ApiAudioDataFormatListType *pcmFormat;
	ApiFpSetAudioFormatCfmType *msgIn;
	ApiFpCcConnectResType *msgConRes;
	ApiFpCcConnectReqType *msgConReq;
	ApiSystemCallIdType SystemCallId;
	ApiCallStatusType status;
	ApiCodecListType *codecs;
	struct call_t *call;
	rsuint16 bufLen;
	rsuint8 *buf;
	char term[15];

	msgIn = (ApiFpSetAudioFormatCfmType *) &m->mail_header;
	call = find_call_by_endpoint_id(msgIn->DestinationId);
	if(!call) return -1;

	buf = NULL;
	bufLen = 0;
	// Send system call ID
	SystemCallId.ApiSubId = API_SUB_CALL_ID;
	SystemCallId.ApiSystemCallId = call->SystemCallId;
	ApiBuildInfoElement(&buf, &bufLen, API_IE_SYSTEM_CALL_ID,
		sizeof(ApiSystemCallIdType), (rsuint8 *) &SystemCallId);

	// Send the codecs we want the handset to use
	codecs = malloc(sizeof(ApiCodecListType) + sizeof(ApiCodecInfoType));
	codecs->NegotiationIndicator = API_NI_POSSIBLE;
	codecs->NoOfCodecs = 2;
//TODO: implement get_handset_codecs(id) in handsets.c and
//match it against what we support (G722 and G726).
	codecs->Codec[0].Codec = API_CT_G722;
	codecs->Codec[0].MacDlcService = API_MDS_1_MD;
	codecs->Codec[0].CplaneRouting = API_CPR_CS;
	codecs->Codec[0].SlotSize = API_SS_LS640;
	codecs->Codec[1].Codec = API_CT_G726;
	codecs->Codec[1].MacDlcService = API_MDS_1_MD;
	codecs->Codec[1].CplaneRouting = API_CPR_CS;
	codecs->Codec[1].SlotSize = API_SS_FS;
	ApiBuildInfoElement(&buf, &bufLen, API_IE_CODEC_LIST,
		sizeof(ApiCodecListType) + sizeof(ApiCodecInfoType) * 
		(codecs->NoOfCodecs - 1), (rsuint8 *) codecs);

	// Send the "air codec to pcm format translation" map
	pcmFormat = malloc(sizeof(ApiAudioDataFormatListType) +
		sizeof(ApiAudioDataFormatInfoType));
	pcmFormat->NoOfCodecs = 2;
	pcmFormat->ApiAudioDataFormatInfo[0].Codec = API_CT_G722;
	pcmFormat->ApiAudioDataFormatInfo[0].ApiAudioDataFormat = 
		AP_DATA_FORMAT_LINEAR_8kHz;
	pcmFormat->ApiAudioDataFormatInfo[1].Codec = API_CT_G726;
	pcmFormat->ApiAudioDataFormatInfo[1].ApiAudioDataFormat = 
		AP_DATA_FORMAT_LINEAR_8kHz;
	ApiBuildInfoElement(&buf, &bufLen, API_IE_AUDIO_DATA_FORMAT,
		sizeof(ApiAudioDataFormatListType) +
		sizeof(ApiAudioDataFormatInfoType) * (pcmFormat->NoOfCodecs - 1),
		(rsuint8 *) pcmFormat);

	// Send "call should become connected"
	status.CallStatusSubId = API_SUB_CALL_STATUS;
	status.CallStatusValue.State = API_CSS_CALL_CONNECT;
	ApiBuildInfoElement(&buf, &bufLen, API_IE_CALL_STATUS,
		sizeof(status), (rsuint8*) &status);

	msgConRes = malloc(sizeof(ApiFpCcConnectResType) - 1 + bufLen);
	msgConRes->Primitive = API_FP_CC_CONNECT_RES;
	msgConRes->CallReference = call->CallReference;
	msgConRes->InfoElementLength = bufLen;

	msgConReq = malloc(sizeof(ApiFpCcConnectReqType) - 1 + bufLen);
	msgConReq->Primitive = API_FP_CC_CONNECT_REQ;
	msgConReq->CallReference = call->CallReference;
	msgConReq->InfoElementLength = bufLen;

	// Send connect request or response
	switch(call->state) {
		case F06_PRESENT:
			memcpy(msgConRes->InfoElement, buf, bufLen);
			mailProto.send(dect_bus, (uint8_t*) msgConRes,
				sizeof(ApiFpCcConnectResType) - 1 + bufLen);
			printf("Call %d state change from %s to %s\n", call->idx,
				cc_state_names[call->state], cc_state_names[F10_ACTIVE]);
			call->state = F10_ACTIVE;
			// Send message to Asterisk to start PCM audio		
			snprintf(term, sizeof(term), "%d", call->epId);
			ubus_send_string_api("dect.api.setup_ind", "terminal", term);
			break;
		case F01_INITIATED:
			printf("[%llu] API_FP_CC_CONNECT_REQ\n", timeSinceStart());
			memcpy(msgConReq->InfoElement, buf, bufLen);
			mailProto.send(dect_bus, (uint8_t*) msgConReq,
				sizeof(ApiFpCcConnectReqType) - 1 + bufLen);
			// Send message to Asterisk to start PCM audio		
			snprintf(term, sizeof(term), "%d", call->epId);
			ubus_send_string_api("dect.api.setup_ind", "terminal", term);
			break;
		default:
			printf("Error, invalid state in %s\n", __FUNCTION__);
//release_req();
			break;
	}

	free(msgConRes);
	free(msgConReq);
	free(pcmFormat);
	free(codecs);
	free(buf);

	return 0;
}


//-------------------------------------------------------------
// Send a list of audio codecs we support to
// handset and play a dial tone.
#if 0
static void audio_format_cfm(busmail_t *m) {
	ApiFpSetAudioFormatCfmType *p = (ApiFpSetAudioFormatCfmType *) &m->mail_header;
	ApiSystemCallIdType SystemCallId;
	rsuint16 ie_block_len;
	ApiInfoElementType *ie_block;
	ApiCallStatusType call_status;
	ApiCodecListType *codecs;
	struct call_t *call;
	char term[15];

	ie_block_len = 0;
	ie_block = NULL;
	print_status(p->Status);
	call = find_call_by_endpoint_id(p->DestinationId);
	if(!call) return;


	SystemCallId.ApiSubId = API_SUB_CALL_ID;
	SystemCallId.ApiSystemCallId = call->SystemCallId;
	ApiBuildInfoElement(&ie_block, &ie_block_len, API_IE_SYSTEM_CALL_ID,
		sizeof(ApiSystemCallIdType), (rsuint8 *) &SystemCallId);

	call_status.CallStatusSubId = API_SUB_CALL_STATUS;
	call_status.CallStatusValue.State = API_CSS_CALL_SETUP_ACK;
	ApiBuildInfoElement(&ie_block, &ie_block_len, API_IE_CALL_STATUS,
		sizeof(ApiCallStatusType),(rsuint8 *) &call_status);

	codecs = malloc(sizeof(ApiCodecListType) + sizeof(ApiCodecInfoType));
	codecs->NegotiationIndicator = API_NI_POSSIBLE;
	codecs->NoOfCodecs = 2;
	codecs->Codec[0].Codec = API_CT_G722;
	codecs->Codec[0].MacDlcService = API_MDS_1_MD;
	codecs->Codec[0].CplaneRouting = API_CPR_CS;
	codecs->Codec[0].SlotSize = API_SS_LS640;
	codecs->Codec[1].Codec = API_CT_G726;
	codecs->Codec[1].MacDlcService = API_MDS_1_MD;
	codecs->Codec[1].CplaneRouting = API_CPR_CS;
	codecs->Codec[1].SlotSize = API_SS_FS;
	ApiBuildInfoElement(&ie_block, &ie_block_len, API_IE_CODEC_LIST,
		sizeof(ApiCodecListType) + sizeof(ApiCodecInfoType), (rsuint8 *) codecs);

	call->state = API_CSS_CALL_SETUP_ACK;
	ApiFpCcSetupAckReqType * ra = (ApiFpCcSetupAckReqType *) 
		malloc(sizeof(ApiFpCcSetupAckReqType) - 1 + ie_block_len);
	ra->Primitive = API_FP_CC_SETUP_ACK_REQ;
	ra->CallReference = call->CallReference;
	ra->Signal = API_CC_SIGNAL_DIAL_TONE_ON;
	ra->ProgressInd = API_IN_BAND_NOT_AVAILABLE;
	ra->InfoElementLength = ie_block_len;
	memcpy(ra->InfoElement, ie_block, ie_block_len);
	printf("[%llu] API_FP_CC_SETUP_ACK_REQ\n", timeSinceStart());	
	mailProto.send(dect_bus, (uint8_t *)ra,
		sizeof(ApiFpCcSetupAckReqType) - 1 + ie_block_len);

	free(codecs);
	free(ie_block);	
	free(ra);
}
#endif


static void setup_ack_cfm(busmail_t *m) {

	ApiFpCcSetupAckCfmType * p = (ApiFpCcSetupAckCfmType *) &m->mail_header;

	printf("[%llu] CallReference: %d\n", timeSinceStart(),
		p->CallReference.Instance.Fp);
	print_status(p->Status);
}



static void info_ind(busmail_t *m) {
	ApiFpCcInfoIndType * p = (ApiFpCcInfoIndType *) &m->mail_header;
	ApiSystemCallIdType SystemCallId;
	rsuint16 ie_block_len;
	ApiInfoElementType *ie_block;
	ApiCallStatusType call_status;
	char json[100];
	char* dialed_nr = NULL;
	struct call_t *call;

	ie_block_len = 0;
	ie_block = NULL;
	call = find_call_by_ref(&p->CallReference);
	if(!call) return;

	printf("Got INFO_IND element %d len %d\n", p->InfoElement[0], p->InfoElementLength);
	printf("[%llu] CallReference: %d\n", timeSinceStart(),
		call->CallReference.Instance.Fp);
	
	/* Did user dial a number? Then send it
	 * to asterisk, otherwise do nothing. */
	dialed_nr = get_dialed_nr((ApiInfoElementType *) p->InfoElement, p->InfoElementLength);
	if(!dialed_nr) return;
	snprintf(json, sizeof(json), "{ \"terminal\": \"%d\", \"dialed_nr\": \"%s\" }",
		call->epId, dialed_nr);
	printf("string: %s\n", json);
	ubus_send_json_string("dect.api.info_ind", json);

	call_status.CallStatusSubId = API_SUB_CALL_STATUS;
	call_status.CallStatusValue.State = API_CSS_CALL_PROC;
	ApiBuildInfoElement(&ie_block, &ie_block_len, API_IE_CALL_STATUS,
		sizeof(ApiCallStatusType), (rsuint8 *) &call_status);

	SystemCallId.ApiSubId = API_SUB_CALL_ID;
	SystemCallId.ApiSystemCallId = call->SystemCallId;
	ApiBuildInfoElement(&ie_block, &ie_block_len, API_IE_SYSTEM_CALL_ID,
		sizeof(call->SystemCallId), (rsuint8 *) &SystemCallId);

	ApiFpCcCallProcReqType * req = (ApiFpCcCallProcReqType*) 
		malloc(sizeof(ApiFpCcCallProcReqType) - 1 + ie_block_len);
	req->Primitive = API_FP_CC_CALL_PROC_REQ;
	req->CallReference = call->CallReference;
	req->ProgressInd = API_IN_BAND_NOT_AVAILABLE;
	req->Signal = API_CC_SIGNAL_CUSTOM_NONE;
	req->InfoElementLength = ie_block_len;
	memcpy(req->InfoElement,(rsuint8*)ie_block, ie_block_len);
	printf("API_FP_CC_CALL_PROC_REQ\n");
	mailProto.send(dect_bus, (uint8_t *) req, 
		sizeof(ApiFpCcCallProcReqType) - 1 + ie_block_len);

	free(ie_block);
	free(req);
	free(dialed_nr);
}



static void call_proc_cfm(busmail_t *m) {
	ApiFpCcCallProcCfmType *p = (ApiFpCcCallProcCfmType *) &m->mail_header;
	ApiSystemCallIdType SystemCallId;
	rsuint16 ie_block_len;
	ApiInfoElementType *ie_block;
	ApiCallStatusType call_status;
	struct call_t *call;
	
	ie_block_len = 0;
	ie_block = NULL;
	printf("[%llu] CallReference: %d\n", timeSinceStart(),
		p->CallReference.Instance.Fp);
	print_status(p->Status);

	call = find_call_by_ref(&p->CallReference);
	if(!call) return;
	
	call_status.CallStatusSubId = API_SUB_CALL_STATUS;
	call_status.CallStatusValue.State = API_CSS_CALL_ALERTING;
	ApiBuildInfoElement(&ie_block, &ie_block_len, API_IE_CALL_STATUS,
		sizeof(ApiCallStatusType), (rsuint8 *) &call_status);

	SystemCallId.ApiSubId = API_SUB_CALL_ID;
	SystemCallId.ApiSystemCallId = call->SystemCallId;
	ApiBuildInfoElement(&ie_block, &ie_block_len, API_IE_SYSTEM_CALL_ID,
		sizeof(call->SystemCallId), (rsuint8 *) &SystemCallId);

	call->state = API_CSS_CALL_ALERTING;
	ApiFpCcAlertReqType * r = malloc(sizeof(ApiFpCcAlertReqType) - 1 + ie_block_len);
	r->Primitive = API_FP_CC_ALERT_REQ;
	r->CallReference = call->CallReference;
	r->Signal = API_CC_SIGNAL_CUSTOM_NONE;
	r->ProgressInd = API_IN_BAND_NOT_AVAILABLE;
	r->InfoElementLength = ie_block_len;
	memcpy(r->InfoElement,(rsuint8*)ie_block, ie_block_len);	
	printf("API_FP_CC_ALERT_REQ in call_proc_cfm()\n");
	mailProto.send(dect_bus, (uint8_t *) r,
		sizeof(ApiFpCcAlertReqType) - 1 + ie_block_len);

	free(ie_block);
	free(r);
}



static void alert_cfm(busmail_t *m) {
	ApiFpCcAlertCfmType *p = (ApiFpCcAlertCfmType *) &m->mail_header;
	ApiSystemCallIdType SystemCallId;
	rsuint16 ie_block_len;
	ApiInfoElementType *ie_block;
	ApiCallStatusType call_status;
	struct call_t *call;

	ie_block_len = 0;
	ie_block = NULL;
	printf("[%llu] CallReference: val %d instancefp %d received\n", timeSinceStart(),
		p->CallReference.Value, p->CallReference.Instance.Fp);
	print_status(p->Status);

	call = find_call_by_ref(&p->CallReference);
	if(!call) return;

	/* Connect handset */
	call_status.CallStatusSubId = API_SUB_CALL_STATUS;
	call_status.CallStatusValue.State = API_CSS_CALL_CONNECT;
	ApiBuildInfoElement(&ie_block, &ie_block_len, API_IE_CALL_STATUS,
		sizeof(ApiCallStatusType), (rsuint8 *) &call_status);
	
	SystemCallId.ApiSubId = API_SUB_CALL_ID;
	SystemCallId.ApiSystemCallId = call->SystemCallId;
	ApiBuildInfoElement(&ie_block, &ie_block_len, API_IE_SYSTEM_CALL_ID,
		sizeof(call->SystemCallId), (rsuint8 *) &SystemCallId);

	call->state = API_CSS_CALL_CONNECT;
	ApiFpCcConnectReqType * req = (ApiFpCcConnectReqType*)
		malloc((sizeof(ApiFpCcConnectReqType) - 1 + ie_block_len));
	req->Primitive = API_FP_CC_CONNECT_REQ;
	req->CallReference = call->CallReference;
	req->InfoElementLength = ie_block_len;
	memcpy(req->InfoElement,(rsuint8*)ie_block, ie_block_len);
	printf("[%llu] API_FP_CC_CONNECT_REQ\n", timeSinceStart());
	mailProto.send(dect_bus, (uint8_t *) req,
		sizeof(ApiFpCcConnectReqType) - 1 + ie_block_len);

	free(ie_block);
	free(req);
}



static void alert_ind(busmail_t *m) {

	ApiFpCcAlertIndType * p = (ApiFpCcAlertIndType *) &m->mail_header;
	ApiFpCcAlertReqType * r;
	ApiInfoElementType * ie_block = NULL;
	rsuint16 ie_block_len = 0;
	ApiCallStatusType call_status;

	printf("[%llu] CallReference: %d\n", timeSinceStart(),
		p->CallReference.Instance.Fp);
	printf("p->InfoElementLength: %d\n", p->InfoElementLength);

/*	if (p->InfoElementLength > 0) {
		codecs = get_codecs((ApiInfoElementType *) p->InfoElement, p->InfoElementLength);
	}*/
}



/* Handset answers */
static void connect_ind(busmail_t *m) {
	ApiFpCcConnectIndType *p = (ApiFpCcConnectIndType *) &m->mail_header;
	ApiInfoElementType *ie_block;
	ApiSystemCallIdType SystemCallId;
	rsuint16 ie_block_len;
	ApiCallStatusType call_status;
	struct call_t *call;

	ie_block_len = 0;
	ie_block = NULL;
	call = find_call_by_ref(&p->CallReference);
	if(!call) return;

	printf("[%llu] CallReference: val %d instancefp %d received\n", timeSinceStart(),
		p->CallReference.Value, p->CallReference.Instance.Fp);
	printf("p->InfoElementLength: %d\n", p->InfoElementLength);
	printf("internal_call: %x\n", call->SystemCallId);

/*	if (p->InfoElementLength > 0) {
		codecs = get_codecs((ApiInfoElementType *) p->InfoElement, p->InfoElementLength);
	}*/
	
	/* Call progress state to initiating handset */
	call_status.CallStatusSubId = API_SUB_CALL_STATUS;
	call_status.CallStatusValue.State = API_CSS_CALL_CONNECT;
	ApiBuildInfoElement(&ie_block, &ie_block_len, API_IE_CALL_STATUS,
		sizeof(ApiCallStatusType), (rsuint8 *) &call_status);

//	ApiBuildInfoElement(&ie_block, &ie_block_len, API_IE_CODEC_LIST,
//		NarrowBandCodecIe->IeLength, (rsuint8 *) NarrowBandCodecIe->IeData);

	SystemCallId.ApiSubId = API_SUB_CALL_ID;
	SystemCallId.ApiSystemCallId = call->SystemCallId;
	ApiBuildInfoElement(&ie_block, &ie_block_len, API_IE_SYSTEM_CALL_ID,
		sizeof(ApiSystemCallIdType), (rsuint8 *) &SystemCallId);
	
	ApiFpCcConnectReqType * req = (ApiFpCcConnectReqType*)
		malloc((sizeof(ApiFpCcConnectReqType) - 1 + ie_block_len));
	req->Primitive = API_FP_CC_CONNECT_REQ;
	req->CallReference = call->CallReference;
	req->InfoElementLength = ie_block_len;
	memcpy(req->InfoElement,(rsuint8*)ie_block, ie_block_len);
//	codecs = get_codecs((ApiInfoElementType *) req->InfoElement, req->InfoElementLength);
	printf("[%llu] API_FP_CC_CONNECT_REQ\n", timeSinceStart());
	printf("[%llu] CallReference: val %d instancefp %d sent\n", timeSinceStart(),
		call->CallReference.Value, call->CallReference.Instance.Fp);
	mailProto.send(dect_bus, (uint8_t *) req,
		sizeof(ApiFpCcConnectReqType) - 1 + ie_block_len);

	free(ie_block);
	free(req);
}



static void connect_cfm(busmail_t *m) {
	ApiFpCcConnectCfmType * p = (ApiFpCcConnectCfmType *) &m->mail_header;
	ApiInfoElementType * ie_block = NULL;
	rsuint16 ie_block_len = 0;
	ApiCallStatusType call_status;

	printf("[%llu] CallReference: %x\n", timeSinceStart(),
		p->CallReference.Value);
	print_status(p->Status);

	if(p->Status == RSS_SUCCESS) {
//		call->status = F10_ACTIVE;
	}
}



static void reject_ind(busmail_t *m) {
	ApiFpCcRejectIndType * p = (ApiFpCcRejectIndType *) &m->mail_header;

	printf("[%llu] CallReference: %x\n", timeSinceStart(),
		p->CallReference.Value);
	printf("Reason: %x\n", p->Reason);
}



static void release_cfm(busmail_t *m) {
	ApiFpCcReleaseCfmType * p = (ApiFpCcReleaseCfmType *) &m->mail_header;

	printf("CallReference: %x\n", p->CallReference.Value);
	print_status(p->Status);
}


static void release_ind(busmail_t *m) {
	ApiFpCcReleaseIndType * p = (ApiFpCcReleaseIndType *) &m->mail_header;
	ApiCallReferenceType terminate_call;
	char term[15];
	struct call_t *call;

	call = find_call_by_ref(&p->CallReference);
	if(!call) return;

	printf("[%llu] CallReference: %x\n", timeSinceStart(),
		p->CallReference.Value);
	printf("Reason: %x\n", p->Reason);
	
	ApiFpCcReleaseResType res = {
		.Primitive = API_FP_CC_RELEASE_RES,
		.CallReference = p->CallReference,
		.Status = RSS_SUCCESS,
		.InfoElementLength = 0,
	};
	
	printf("API_FP_CC_RELEASE_RES\n");
	mailProto.send(dect_bus, (uint8_t *)&res, sizeof(res));

	snprintf(term, sizeof(term), "%d", call->epId);

	ubus_send_string_api("dect.api.release_ind", "terminal", term);

	free_call_slot(call);
}





#if 0
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
#endif




//-------------------------------------------------------------
// Handle incomming mail messages from external Dect FP
void external_call_mail_handler(packet_t *p) {
	busmail_t *m = (busmail_t*) p->data;

	// Is the mail intended for us?
	if (m->task_id != 1) return;

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






//-------------------------------------------------------------
void external_call_init(void * bus) {
	int i;

	dect_bus = bus;

	memset(calls, 0, sizeof(calls));
	for(i = 0; i < MAX_CALLS; i++) {
		calls[i].state = F00_NULL;
		calls[i].idx = i;
	}

	mailProto.add_handler(bus, external_call_mail_handler);
}

