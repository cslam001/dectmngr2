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
static int parse_info_elements(struct call_t *call, rsuint8 *InfoElements, rsuint16 InfoElementLength);
static int release_req(struct call_t *call, ApiCcReleaseReasonType reason);



#if 0
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
	int keypad_len;
	char * keypad_str;
	
	info = ApiGetInfoElement(InfoElement, InfoElementLength, API_IE_MULTIKEYPAD);
	if(!info) return NULL;
	if(!info->IeLength) return NULL;

	printf("API_IE_MULTIKEYPAD\n");
	keypad = (ApiCodecListType*) &info->IeData[0];
	keypad_len = info->IeLength;
	keypad_str = (char*) malloc(keypad_len + 1);
	memcpy(keypad_str, keypad, keypad_len);
	keypad_str[keypad_len] = '\0';
	printf("keypad: %s\n", keypad_str);

	return keypad_str;
}

static ApiCodecListType * get_codecs(ApiInfoElementType * InfoElement, rsuint16 InfoElementLength) {
	ApiInfoElementType * info;
	ApiCodecListType * list;
	
	info = ApiGetInfoElement(InfoElement, InfoElementLength, API_IE_CODEC_LIST);
	if ( info && info->IeLength > 0 ) {
		printf("API_IE_CODEC_LIST\n");
		list = (ApiCodecListType*) &info->IeData[0];
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
		printf("GotLineId\n");
		line_id = malloc(sizeof(ApiLineIdValueType));
		memcpy(line_id, &info->IeData[0], info->IeLength);

		return line_id;
	}
	return NULL;
}
#endif



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
	return NULL;
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
	ApiFpCcSetupIndType *msgIn = (ApiFpCcSetupIndType*) &m->mail_header;
	ApiFpCcSetupResType reply;
	struct call_t *call;
	
	// First check prerequisites. Can the call switch to next state?
	reply.Primitive = API_FP_CC_SETUP_RES;
	reply.CallReference.Instance.Fp = msgIn->CallReference.Instance.Fp;
	reply.CallReference.Instance.Host = msgIn->TerminalId;
	reply.AudioId.IntExtAudio = API_IEA_EXT;
	reply.AudioId.AudioEndPointId = msgIn->TerminalId - 1;

	/* Don't accept source terminal ID higher than can fit into
	 * the four bit field "reply.CallReference.Instance.Host" */
	if(msgIn->TerminalId > 15) {
		printf("Error, invalid terminal ID\n");
		reply.Status = RSS_FAILED;
		mailProto.send(dect_bus, (uint8_t*) &reply, sizeof(ApiFpCcSetupResType));
		return -1;
	}

	// Did FP allocate a system call ID?
	if(!msgIn->InfoElementLength || 
			!ApiGetInfoElement((ApiInfoElementType*) msgIn->InfoElement,
			msgIn->InfoElementLength, API_IE_SYSTEM_CALL_ID)) {
		printf("Error, got no system call ID\n");
		reply.Status = RSS_MISSING_PARAMETER;
		mailProto.send(dect_bus, (uint8_t*) &reply, sizeof(ApiFpCcSetupResType));
		return -1;
	}

	// OK, we accept the request. Try to setup a new phone call.
	call = find_free_call_slot();
	if(!call) {
		printf("Error, no free call slot\n");
		reply.Status = RSS_NO_RESOURCE;
		mailProto.send(dect_bus, (uint8_t*) &reply, sizeof(ApiFpCcSetupResType));
		return -1;
	}

	// Change state before we parse the info elements below
	printf("Call %d state change from %s to %s\n", call->idx,
		cc_state_names[call->state], cc_state_names[F01_INITIATED]);
	call->state = F01_INITIATED;
	call->CallReference = reply.CallReference;
	call->TerminalId = msgIn->TerminalId;
	call->BasicService = msgIn->BasicService;
	call->epId = reply.AudioId.AudioEndPointId;

	parse_info_elements(call, msgIn->InfoElement, msgIn->InfoElementLength);

	reply.Status = RSS_SUCCESS;
	mailProto.send(dect_bus, (uint8_t*)&reply, sizeof(ApiFpCcSetupResType));

	// Request PCM audio format
	return audio_format_req(call);
}



//-------------------------------------------------------------
// Initiate an incomming external call from outer world
// going to a handset.
int setup_req(uint32_t termId, int pcmId) {
const char number[] = { '0', '1', '2', '3', '4' };
	ApiCallingNumberType callerNumber;
	struct terminal_t *terminal;
	ApiCallStatusType status;
	ApiCodecListType *codecs;
	ApiLineIdType line;
	ApiFpCcSetupReqType *msg;
	struct call_t *call;
	rsuint16 bufLen;
	rsuint8 *buf;
	int i;

	// Find the handset in our list of registereds
	for(i = 0; i < MAX_NR_HANDSETS && handsets.terminal[i].id != termId; i++);
	if(i == MAX_NR_HANDSETS) return -1;
	terminal = &handsets.terminal[i];

	// Put the call in our list of current ongoing calls
	call = find_free_call_slot();
	if(!call) return -1;
	call->CallReference.Instance.Fp = 0;
	call->CallReference.Instance.Host = termId;
	call->epId = pcmId;
	call->TerminalId = termId;
	call->BasicService = terminal->BasicService;

	buf = NULL;
	bufLen = 0;
	// Send "call setup"
	status.CallStatusSubId = API_SUB_CALL_STATUS;
	status.CallStatusValue.State = API_CSS_CALL_SETUP;
	ApiBuildInfoElement((ApiInfoElementType**) &buf, &bufLen,
		API_IE_CALL_STATUS, sizeof(status), (rsuint8*) &status);

	line.ApiSubId = API_SUB_LINE_ID_EXT_LINE_ID;
	line.ApiLineValue.Value = 1;
	ApiBuildInfoElement((ApiInfoElementType**) &buf, &bufLen,
		API_IE_LINE_ID, sizeof(line), (rsuint8*) &line);

	// Send callers phone number
	callerNumber.NumberType = ANT_NATIONAL;
	callerNumber.Npi = ANPI_NATIONAL;
	callerNumber.PresentationInd = API_PRESENTATION_ALLOWED;
	callerNumber.ScreeningInd = API_USER_PROVIDED_NOT_SCREENED;
	callerNumber.NumberLength = sizeof(number);
	memcpy(callerNumber.Number, number, sizeof(number));
	ApiBuildInfoElement((ApiInfoElementType**) &buf, &bufLen,
		API_IE_CALLING_PARTY_NUMBER, sizeof(callerNumber) + sizeof(number),
		(rsuint8*) &callerNumber);

	// Send list of codecs we support to handset
	codecs = calloc(1, sizeof(ApiCodecListType) + sizeof(ApiCodecInfoType));
	codecs->NegotiationIndicator = API_NI_POSSIBLE;
	if(terminal->BasicService == API_WIDEBAND_SPEECH) {
		codecs->NoOfCodecs = 2;
		codecs->Codec[0].Codec = API_CT_G722;
		codecs->Codec[0].MacDlcService = API_MDS_1_MD;
		codecs->Codec[0].CplaneRouting = API_CPR_CS;
		codecs->Codec[0].SlotSize = API_SS_LS640;
		codecs->Codec[1].Codec = API_CT_G726;
		codecs->Codec[1].MacDlcService = API_MDS_1_MD;
		codecs->Codec[1].CplaneRouting = API_CPR_CS;
		codecs->Codec[1].SlotSize = API_SS_FS;
	}
	else {
		codecs->NoOfCodecs = 1;
		codecs->Codec[0].Codec = API_CT_G726;
		codecs->Codec[0].MacDlcService = API_MDS_1_MD;
		codecs->Codec[0].CplaneRouting = API_CPR_CS;
		codecs->Codec[0].SlotSize = API_SS_FS;
	}
	ApiBuildInfoElement((ApiInfoElementType**) &buf, &bufLen,
		API_IE_CODEC_LIST, sizeof(ApiCodecListType) + sizeof(ApiCodecInfoType) *
		(codecs->NoOfCodecs - 1), (rsuint8*) codecs);

	msg = calloc(1, sizeof(ApiFpCcSetupReqType) - 1 + bufLen);
	msg->Primitive = API_FP_CC_SETUP_REQ;
	msg->CallReference = call->CallReference;
	msg->TerminalId = call->TerminalId;
	msg->AudioId.IntExtAudio = API_IEA_EXT;
	msg->AudioId.AudioEndPointId = call->epId;
	msg->AudioId.SourceTerminalId = 0;
	msg->BasicService = call->BasicService;
	msg->CallClass = API_CC_NORMAL;
	msg->Signal = API_CC_SIGNAL_ALERT_ON_PATTERN_1;
	msg->InfoElementLength = bufLen;
	memcpy(msg->InfoElement, buf, bufLen);
	
	mailProto.send(dect_bus, (uint8_t*) msg,
		sizeof(ApiFpCcSetupReqType) - 1 + bufLen);

	printf("Call %d state change from %s to %s\n", call->idx,
		cc_state_names[call->state], cc_state_names[F06_PRESENT]);
	call->state = F06_PRESENT;

	free(codecs);
	free(buf);
	free(msg);

	return 0;
}



//-------------------------------------------------------------
// FP responds to an incomming external call setup request
// Can it continue or does it deny the setup?
static int setup_cfm(busmail_t *m) {
	ApiFpCcSetupCfmType *msgIn;
	struct call_t *call;
	int doProceed;

	msgIn = (ApiFpCcSetupCfmType*) &m->mail_header;
	call = find_call_by_ref(&msgIn->CallReference);
	if(!call) return -1;

	/* Save the unique system call ID the
	 * FP generated for us. */
	call->SystemCallId = msgIn->SystemCallId;
	call->CallReference = msgIn->CallReference;
	call->BasicService = msgIn->BasicService;

	/* Verify that audio has been routed as we requested
	 * and that FP want to proceed with the call setup. */
	doProceed = (msgIn->Status == RSS_SUCCESS);
	doProceed &= (msgIn->AudioId.IntExtAudio == API_IEA_EXT);
	doProceed &= (msgIn->AudioId.AudioEndPointId == call->epId);
	doProceed &= (call->state != F19_RELEASE_PEND);

	if(!doProceed) {
		printf("Call %d state change from %s to %s\n", call->idx,
			cc_state_names[call->state], cc_state_names[F19_RELEASE_PEND]);
		call->state = F19_RELEASE_PEND;
		return release_req(call, API_RR_NORMAL);
	}

	return 0;	
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
	mailProto.send(dect_bus, (uint8_t*) &pcmFormat, sizeof(pcmFormat));

	return 0;
}


//-------------------------------------------------------------
// Send a list of audio codecs we support to handset
// and tell Asterisk to open audio channel.
static int audio_format_cfm(busmail_t *m) {
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
	char term[32];

	msgIn = (ApiFpSetAudioFormatCfmType*) &m->mail_header;
	call = find_call_by_endpoint_id(msgIn->DestinationId);
	if(!call) return -1;

	buf = NULL;
	bufLen = 0;
	// Send system call ID
	SystemCallId.ApiSubId = API_SUB_CALL_ID;
	SystemCallId.ApiSystemCallId = call->SystemCallId;
	ApiBuildInfoElement((ApiInfoElementType**) &buf, &bufLen,
		API_IE_SYSTEM_CALL_ID, sizeof(ApiSystemCallIdType),
		(rsuint8*) &SystemCallId);

	// Send the codecs we want the handset to use
	codecs = calloc(1, sizeof(ApiCodecListType) + sizeof(ApiCodecInfoType));
	codecs->NegotiationIndicator = API_NI_POSSIBLE;
	if(call->BasicService == API_WIDEBAND_SPEECH) {
		codecs->NoOfCodecs = 2;
		codecs->Codec[0].Codec = API_CT_G722;
		codecs->Codec[0].MacDlcService = API_MDS_1_MD;
		codecs->Codec[0].CplaneRouting = API_CPR_CS;
		codecs->Codec[0].SlotSize = API_SS_LS640;
		codecs->Codec[1].Codec = API_CT_G726;
		codecs->Codec[1].MacDlcService = API_MDS_1_MD;
		codecs->Codec[1].CplaneRouting = API_CPR_CS;
		codecs->Codec[1].SlotSize = API_SS_FS;
	}
	else {
		codecs->NoOfCodecs = 1;
		codecs->Codec[0].Codec = API_CT_G726;
		codecs->Codec[0].MacDlcService = API_MDS_1_MD;
		codecs->Codec[0].CplaneRouting = API_CPR_CS;
		codecs->Codec[0].SlotSize = API_SS_FS;
	}
	ApiBuildInfoElement((ApiInfoElementType**) &buf, &bufLen,
		API_IE_CODEC_LIST, sizeof(ApiCodecListType) + sizeof(ApiCodecInfoType) *
		(codecs->NoOfCodecs - 1), (rsuint8*) codecs);

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
	ApiBuildInfoElement((ApiInfoElementType**) &buf, &bufLen,
		API_IE_AUDIO_DATA_FORMAT, sizeof(ApiAudioDataFormatListType) +
		sizeof(ApiAudioDataFormatInfoType) * (pcmFormat->NoOfCodecs - 1),
		(rsuint8*) pcmFormat);

	// Send "call should become connected"
	status.CallStatusSubId = API_SUB_CALL_STATUS;
	status.CallStatusValue.State = API_CSS_CALL_CONNECT;
	ApiBuildInfoElement((ApiInfoElementType**) &buf, &bufLen,
		API_IE_CALL_STATUS, sizeof(status), (rsuint8*) &status);

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
//			ubus_send_string_to("dect.api.setup_ind", "terminal", term);
printf("todo: asterisk audio open\n");
			break;

		case F01_INITIATED:
			printf("[%llu] API_FP_CC_CONNECT_REQ\n", timeSinceStart());
			memcpy(msgConReq->InfoElement, buf, bufLen);
			mailProto.send(dect_bus, (uint8_t*) msgConReq,
				sizeof(ApiFpCcConnectReqType) - 1 + bufLen);
			// Send message to Asterisk to start PCM audio		
			snprintf(term, sizeof(term), "%d", call->epId);
			ubus_send_string_to("dect.api.setup_ind", "terminal", term);
			break;

		case F00_NULL:
			snprintf(term, sizeof(term), "%d", call->epId);
			ubus_send_string_to("dect.api.release_ind", "terminal", term);
			free_call_slot(call);
			break;

		case F19_RELEASE_PEND:
			release_req(call, API_RR_NORMAL);
			break;

		default:
			printf("Error, invalid state in %s\n", __FUNCTION__);
			release_req(call, API_RR_UNKNOWN);
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
// We receive various data such as dialed number etc.
static int info_ind(busmail_t *m) {
	ApiFpCcInfoIndType *msgIn;
	struct call_t *call;

	msgIn = (ApiFpCcInfoIndType*) &m->mail_header;
	call = find_call_by_ref(&msgIn->CallReference);
	if(!call) return -1;
	call->CallReference = msgIn->CallReference;

	printf("Got INFO_IND element %d len %d\n", msgIn->InfoElement[0],
		msgIn->InfoElementLength);
	return parse_info_elements(call, msgIn->InfoElement,
		msgIn->InfoElementLength);
}



//-------------------------------------------------------------
// Parse list of embedded info elements in mail message
static int parse_info_elements(struct call_t *call, rsuint8 *InfoElements, rsuint16 InfoElementLength) {
	const char *ubusDialKeys[] = { "terminal", "dialed_nr" };
	const char ubusInfoIndPath[] = "dect.api.info_ind";
	const unsigned int maxUbusValLen = 64;
	const unsigned int maxUbusValCnt = 4;
	ApiInfoElementType *InfoElement;
	ApiCalledNumberType *calledNum;
	char *ubusVals[maxUbusValCnt];
	ApiSystemCallIdType *sysId;
	unsigned int readLen, i;


	InfoElement = (ApiInfoElementType*) InfoElements;
	readLen = 0;
	for(i = 0; i < maxUbusValCnt; i++) ubusVals[i] = malloc(maxUbusValLen);

	while((unsigned int) InfoElementLength - readLen >= sizeof(ApiInfoElementType)) {
		printf("Info element 0x%x, len %u tot len %u\n", InfoElement->Ie, InfoElement->IeLength, InfoElementLength);

		switch(InfoElement->Ie) {
			// Extract the number handset dialed and send to Asterisk
			case API_IE_MULTIKEYPAD:
				if(call->state == F00_NULL || call->state == F19_RELEASE_PEND) break;
				if(maxUbusValLen < (unsigned int) InfoElement->IeLength + 1) break;	// To many digits?
				snprintf(ubusVals[0], maxUbusValLen, "%u", call->epId);
				snprintf(ubusVals[1], InfoElement->IeLength + 1,
					"%s", InfoElement->IeData);
				ubus_send_strings(ubusInfoIndPath, ubusDialKeys,
					(const char**) ubusVals, 2);
				break;

			// Extract the number handset dialed and send to Asterisk
			case API_IE_CALLED_NUMBER:
				if(call->state == F00_NULL || call->state == F19_RELEASE_PEND) break;
				calledNum = (ApiCalledNumberType*) InfoElement->IeData;
				if(maxUbusValLen < (unsigned int) InfoElement->IeLength + 1) break;	// To many digits?
				snprintf(ubusVals[0], maxUbusValLen, "%u", call->epId);
				snprintf(ubusVals[1], calledNum->NumberLength + 1,
					"%s", calledNum->Number);
				ubus_send_strings(ubusInfoIndPath, ubusDialKeys,
					(const char**) ubusVals, 2);
				break;

			// Extract SystemCallId which FP should have assigned for us
			case API_IE_SYSTEM_CALL_ID:
				sysId = (ApiSystemCallIdType*) InfoElement->IeData;
				switch(sysId->ApiSubId) {
					case API_SUB_CALL_ID:
					case API_SUB_CALL_ID_UPDATE:
						call->SystemCallId = sysId->ApiSystemCallId;
						printf("API_IE_SYSTEM_CALL_ID %d\n", call->SystemCallId);
						break;
					default:
						break;
				}
				break;

			default:
				break;
		}

		readLen += sizeof(ApiInfoElementType) - 1u +
			(unsigned int) InfoElement->IeLength;
		InfoElement = (ApiInfoElementType*) (InfoElements + readLen);
	}

	for(i = 0; i < maxUbusValCnt; i++) free(ubusVals[i]);

	return 0;
}


#if 0
static void call_proc_cfm(busmail_t *m) {
	ApiFpCcCallProcCfmType *p = (ApiFpCcCallProcCfmType*) &m->mail_header;
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
		sizeof(ApiCallStatusType), (rsuint8*) &call_status);

	SystemCallId.ApiSubId = API_SUB_CALL_ID;
	SystemCallId.ApiSystemCallId = call->SystemCallId;
	ApiBuildInfoElement(&ie_block, &ie_block_len, API_IE_SYSTEM_CALL_ID,
		sizeof(call->SystemCallId), (rsuint8*) &SystemCallId);

	call->state = API_CSS_CALL_ALERTING;
	ApiFpCcAlertReqType * r = malloc(sizeof(ApiFpCcAlertReqType) - 1 + ie_block_len);
	r->Primitive = API_FP_CC_ALERT_REQ;
	r->CallReference = call->CallReference;
	r->Signal = API_CC_SIGNAL_CUSTOM_NONE;
	r->ProgressInd = API_IN_BAND_NOT_AVAILABLE;
	r->InfoElementLength = ie_block_len;
	memcpy(r->InfoElement,(rsuint8*)ie_block, ie_block_len);	
	printf("API_FP_CC_ALERT_REQ in call_proc_cfm()\n");
	mailProto.send(dect_bus, (uint8_t*) r,
		sizeof(ApiFpCcAlertReqType) - 1 + ie_block_len);

	free(ie_block);
	free(r);
}
#endif

#if 0
static void alert_cfm(busmail_t *m) {
	ApiFpCcAlertCfmType *p = (ApiFpCcAlertCfmType*) &m->mail_header;
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
		sizeof(ApiCallStatusType), (rsuint8*) &call_status);
	
	SystemCallId.ApiSubId = API_SUB_CALL_ID;
	SystemCallId.ApiSystemCallId = call->SystemCallId;
	ApiBuildInfoElement(&ie_block, &ie_block_len, API_IE_SYSTEM_CALL_ID,
		sizeof(call->SystemCallId), (rsuint8*) &SystemCallId);

	call->state = API_CSS_CALL_CONNECT;
	ApiFpCcConnectReqType * req = (ApiFpCcConnectReqType*)
		malloc((sizeof(ApiFpCcConnectReqType) - 1 + ie_block_len));
	req->Primitive = API_FP_CC_CONNECT_REQ;
	req->CallReference = call->CallReference;
	req->InfoElementLength = ie_block_len;
	memcpy(req->InfoElement,(rsuint8*)ie_block, ie_block_len);
	printf("[%llu] API_FP_CC_CONNECT_REQ\n", timeSinceStart());
	mailProto.send(dect_bus, (uint8_t*) req,
		sizeof(ApiFpCcConnectReqType) - 1 + ie_block_len);

	free(ie_block);
	free(req);
}
#endif


//-------------------------------------------------------------
// Handset alert us when it start ringing
static int alert_ind(busmail_t *m) {
	ApiFpCcAlertIndType *msgIn;
	struct call_t *call;

	msgIn = (ApiFpCcAlertIndType*) &m->mail_header;
	call = find_call_by_ref(&msgIn->CallReference);
	if(!call) return -1;
	call->CallReference = msgIn->CallReference;

	parse_info_elements(call, msgIn->InfoElement, msgIn->InfoElementLength);

	switch(call->state) {
		case F19_RELEASE_PEND:
			return release_req(call, API_RR_NORMAL);
		
		default:
			break;
	}			

	return 0;
}



//-------------------------------------------------------------
// Handset accepts an incomming call and want
// to processed with setup.
static int connect_ind(busmail_t *m) {
	ApiFpCcConnectIndType *msgIn;
	struct call_t *call;

	msgIn = (ApiFpCcConnectIndType*) &m->mail_header;
	call = find_call_by_ref(&msgIn->CallReference);
	if(!call) return -1;
	call->CallReference = msgIn->CallReference;

	parse_info_elements(call, msgIn->InfoElement, msgIn->InfoElementLength);

	return audio_format_req(call);
}



//-------------------------------------------------------------
// Handset became fully off hook and audio is streaming
static int connect_cfm(busmail_t *m) {
	ApiFpCcConnectCfmType *msgIn;
	struct call_t *call;

	msgIn = (ApiFpCcConnectCfmType*) &m->mail_header;
	call = find_call_by_ref(&msgIn->CallReference);
	if(!call) return -1;

	parse_info_elements(call, msgIn->InfoElement, msgIn->InfoElementLength);

	switch(call->state) {
		case F19_RELEASE_PEND:
			return release_req(call, API_RR_NORMAL);

		default:
			if(msgIn->Status == RSS_SUCCESS) {
				printf("Call %d state change from %s to %s\n", call->idx,
					cc_state_names[call->state], cc_state_names[F10_ACTIVE]);
				call->state = F10_ACTIVE;
			}
			break;
	}

	return 0;
}



//-------------------------------------------------------------
// Handset has rejected to answer a call
static int reject_ind(busmail_t *m) {
	ApiFpCcRejectIndType *msgIn;
	struct call_t *call;

	msgIn = (ApiFpCcRejectIndType*) &m->mail_header;
	call = find_call_by_ref(&msgIn->CallReference);
	if(!call) return -1;
	call->CallReference = msgIn->CallReference;

	parse_info_elements(call, msgIn->InfoElement, msgIn->InfoElementLength);

	printf("Call %d state change from %s to %s\n", call->idx,
		cc_state_names[call->state], cc_state_names[F19_RELEASE_PEND]);
	call->state = F19_RELEASE_PEND;

	// Request shut down of PCM audio
	return audio_format_req(call);
}



//-------------------------------------------------------------
// We asked for termination of a call and the phone agrees
static int release_cfm(busmail_t *m) {
	ApiFpCcReleaseCfmType *msgIn;
	struct call_t *call;

	msgIn = (ApiFpCcReleaseCfmType*) &m->mail_header;
	call = find_call_by_ref(&msgIn->CallReference);
	if(!call) return -1;

	parse_info_elements(call, msgIn->InfoElement, msgIn->InfoElementLength);

	switch(call->state) {
		case F19_RELEASE_PEND:
			printf("Call %d state change from %s to %s\n", call->idx,
				cc_state_names[call->state], cc_state_names[F00_NULL]);
			call->state = F00_NULL;
			// Request shut down of PCM audio
			return audio_format_req(call);
			break;

		default:
			break;
	}

	return 0;
}



//-------------------------------------------------------------
// We ask the handset to terminate a call and
// set the call to "pending release".
static int release_req(struct call_t *call, ApiCcReleaseReasonType reason) {
	ApiSystemCallIdType SystemCallId;
	ApiFpCcReleaseReqType *msgOut;
	rsuint16 bufLen;
	rsuint8 *buf;

	buf = NULL;
	bufLen = 0;
	// Send system call ID
	SystemCallId.ApiSubId = API_SUB_CALL_ID;
	SystemCallId.ApiSystemCallId = call->SystemCallId;
	ApiBuildInfoElement((ApiInfoElementType**) &buf, &bufLen,
		API_IE_SYSTEM_CALL_ID, sizeof(ApiSystemCallIdType),
		(rsuint8*) &SystemCallId);

	msgOut = malloc(sizeof(ApiFpCcReleaseReqType) - 1 + bufLen);
	msgOut->Primitive = API_FP_CC_RELEASE_REQ;
	msgOut->CallReference = call->CallReference;
	msgOut->Reason = reason;
	msgOut->InfoElementLength = bufLen;
	
	memcpy(msgOut->InfoElement, buf, bufLen);
	printf("API_FP_CC_RELEASE_REQ\n");
	mailProto.send(dect_bus, (uint8_t*) msgOut,
		sizeof(ApiFpCcReleaseReqType) - 1 + bufLen);

	if(call->state != F19_RELEASE_PEND) {
		printf("Call %d state change from %s to %s\n", call->idx,
			cc_state_names[call->state], cc_state_names[F19_RELEASE_PEND]);
		call->state = F19_RELEASE_PEND;
	}

	return 0;	
}



//-------------------------------------------------------------
// Handset user has pressed the on hook key, we terminate the call.
static int release_ind(busmail_t *m) {
	ApiSystemCallIdType SystemCallId;
	ApiFpCcReleaseResType *msgOut;
	ApiFpCcReleaseIndType *msgIn;
	struct call_t *call;
	rsuint16 bufLen;
	rsuint8 *buf;

	msgIn = (ApiFpCcReleaseIndType*) &m->mail_header;
	call = find_call_by_ref(&msgIn->CallReference);
	if(!call) return -1;
	call->CallReference = msgIn->CallReference;

	parse_info_elements(call, msgIn->InfoElement, msgIn->InfoElementLength);

	buf = NULL;
	bufLen = 0;
	// Send system call ID
	SystemCallId.ApiSubId = API_SUB_CALL_ID;
	SystemCallId.ApiSystemCallId = call->SystemCallId;
	ApiBuildInfoElement((ApiInfoElementType**) &buf, &bufLen,
		API_IE_SYSTEM_CALL_ID, sizeof(ApiSystemCallIdType),
		(rsuint8*) &SystemCallId);

	// Send acknowledge, we will disconnect the call
	msgOut = malloc(sizeof(ApiFpCcReleaseResType) - 1 + bufLen);
	msgOut->Primitive = API_FP_CC_RELEASE_RES;
	msgOut->CallReference = call->CallReference;
	msgOut->InfoElementLength = bufLen;
	msgOut->Status = RSS_SUCCESS;

	memcpy(msgOut->InfoElement, buf, bufLen);
	printf("API_FP_CC_RELEASE_RES\n");
	mailProto.send(dect_bus, (uint8_t*) msgOut,
		sizeof(ApiFpCcReleaseResType) - 1 + bufLen);

	printf("Call %d state change from %s to %s\n", call->idx,
		cc_state_names[call->state], cc_state_names[F00_NULL]);
	call->state = F00_NULL;

	// Request shut down of PCM audio
	return audio_format_req(call);
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
	mailProto.send(dect_bus, (uint8_t*)&req, sizeof(ApiFpCcSetupReqType));
	return;
}
#endif




//-------------------------------------------------------------
// Handle incomming mail messages from external Dect FP
static void external_call_mail_handler(packet_t *p) {
	busmail_t *msgIn;
	int res;

	msgIn = (busmail_t*) p->data;
	res = 0;

	// Is the mail intended for us?
	if(msgIn->task_id != 1) return;

	switch(msgIn->mail_header) {
		case API_FP_CC_SETUP_IND:
			res = setup_ind(msgIn);
			break;
	
		case API_FP_CC_SETUP_CFM:
			res = setup_cfm(msgIn);
			break;
	
		case API_FP_CC_RELEASE_IND:
			res = release_ind(msgIn);
			break;
	
		case API_FP_CC_RELEASE_CFM:
			res = release_cfm(msgIn);
			break;
	
		case API_FP_CC_REJECT_IND:
			res = reject_ind(msgIn);
			break;
	
		case API_FP_CC_CONNECT_IND:
			res = connect_ind(msgIn);
			break;
	
		case API_FP_CC_CONNECT_CFM:
			res = connect_cfm(msgIn);
			break;
	
		case API_FP_CC_ALERT_IND:
			res = alert_ind(msgIn);
			break;
	
//		case API_FP_CC_ALERT_CFM:
//			alert_cfm(m);
			break;
	
//		case API_FP_CC_SETUP_ACK_CFM:
//			setup_ack_cfm(m);
//			break;
	
		case API_FP_CC_INFO_IND:
			res = info_ind(msgIn);
			break;
	
		case API_FP_SET_AUDIO_FORMAT_CFM:
			res = audio_format_cfm(msgIn);
			break;
	
//		case API_FP_CC_CALL_PROC_CFM:
//			call_proc_cfm(m);
//			break;

		default:
			break;	
	}

	if(res) {
		printf("Warning: failed primitive 0x%x\n", msgIn->mail_header);
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

