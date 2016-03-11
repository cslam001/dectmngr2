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
#include "dect.h"


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
	Q22_CC_RELEASE_COM,
};


struct call_t {
	enum cc_states_t state;
	int idx;

	/* Caller id (caller number) */
	char *cid;

	/* "Call ID’s are assigned by the FP and are uniquely defined
	 * system-wide. This allows proper handling of asynchronous
	 * messages (ApiSystemCallIdType)." */
	ApiSystemCallIdType SystemCallId;

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
	"F19-release pending",
	"Q22-release received",
};


static struct call_t calls[MAX_CALLS];
static void * dect_bus;


//-------------------------------------------------------------
static int audio_format_req(struct call_t *call);
static int parse_info_elements(struct call_t *call, rsuint8 *InfoElements, rsuint16 InfoElementLength);
static int fill_info_elements(struct call_t *call, rsuint8 **buf, rsuint16 *bufLen);
static int release_req(struct call_t *call, ApiCcReleaseReasonType reason);



//-------------------------------------------------------------
static struct call_t* find_free_call_slot(void) {
	int i;

	for(i = 0; i < MAX_CALLS; i++) {
		if(calls[i].state != F00_NULL) continue;

		if(calls[i].SystemCallId.ApiSubId == API_SUB_CALL_ID_INVALID &&
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
		// Do NOT check for calls[i].state == F00_NULL here!
		//if(calls[i].state == F00_NULL) continue;

		if(calls[i].CallReference.Value && calls[i].epId == epId) {
			return &calls[i];
		}
	}

	return NULL;
}


//-------------------------------------------------------------
static struct call_t* find_call_by_ref(ApiCallReferenceType *CallReference) {
	int i;

	for(i = 0; i < MAX_CALLS; i++) {
		if(calls[i].state == F00_NULL) continue;

		if(calls[i].CallReference.Instance.Fp == CallReference->Instance.Fp) {
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
	int idx;

	idx = call->idx;															// Save the index number past mem clear
	printf("Freeing call slot %d\n", idx);
	free(call->cid);
	memset(call, 0, sizeof(struct call_t));
	call->state = F00_NULL;
	call->idx = idx;
	call->SystemCallId.ApiSubId = API_SUB_CALL_ID_INVALID;
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
	struct terminal_t *terminal;
	ApiCallStatusType status;
	ApiFpCcSetupReqType *msg;
	struct call_t *call;
	rsuint16 bufLen;
	rsuint8 *buf;
	int i;

	call = find_call_by_endpoint_id(pcmId);
	if(call) {
		termId = call->TerminalId;
printf("TODO: send CID to handset second time\n");
		return 0;
	}

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
	if(fill_info_elements(call, &buf, &bufLen)) return -1;

	// Send "call setup"
	status.CallStatusSubId = API_SUB_CALL_STATUS;
	status.CallStatusValue.State = API_CSS_CALL_SETUP;
	ApiBuildInfoElement((ApiInfoElementType**) &buf, &bufLen,
		API_IE_CALL_STATUS, sizeof(status), (rsuint8*) &status);

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
	call->SystemCallId.ApiSystemCallId = msgIn->SystemCallId;
	call->SystemCallId.ApiSubId = API_SUB_CALL_ID;
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
	ApiFpSetAudioFormatCfmType *msgIn;
	ApiFpCcConnectResType *msgConRes;
	ApiCallStatusType status;
	struct call_t *call;
	rsuint16 bufLen;
	rsuint8 *buf;
	int res;

	msgIn = (ApiFpSetAudioFormatCfmType*) &m->mail_header;
	call = find_call_by_endpoint_id(msgIn->DestinationId);
	if(!call) return -1;

	res = 0;
	buf = NULL;
	bufLen = 0;
	if(fill_info_elements(call, &buf, &bufLen)) return -1;

	// Send "call should become connected"
	status.CallStatusSubId = API_SUB_CALL_STATUS;
	status.CallStatusValue.State = API_CSS_CALL_CONNECT;
	ApiBuildInfoElement((ApiInfoElementType**) &buf, &bufLen,
		API_IE_CALL_STATUS, sizeof(status), (rsuint8*) &status);

	msgConRes = malloc(sizeof(ApiFpCcConnectResType) - 1 + bufLen);
	msgConRes->Primitive = API_FP_CC_CONNECT_RES;
	msgConRes->CallReference = call->CallReference;
	msgConRes->InfoElementLength = bufLen;

	// Send connect request or response
	switch(call->state) {
		case F06_PRESENT:
			memcpy(msgConRes->InfoElement, buf, bufLen);
			mailProto.send(dect_bus, (uint8_t*) msgConRes,
				sizeof(ApiFpCcConnectResType) - 1 + bufLen);
			printf("Call %d state change from %s to %s\n", call->idx,
				cc_state_names[call->state], cc_state_names[F10_ACTIVE]);
			call->state = F10_ACTIVE;
			// Send message to Asterisk handset goes "off hook"
			res = asterisk_call(call->TerminalId, call->epId, -1, NULL);
			break;

		case F01_INITIATED:
			// Send message to Asterisk handset goes "off hook"
			res = asterisk_call(call->TerminalId, call->epId, -1, NULL);
			break;

		case Q22_CC_RELEASE_COM:
			if(asterisk_call(call->TerminalId, -1, call->epId, NULL)) {
				free_call_slot(call);
			}
			break;

		case F00_NULL:
			asterisk_call(call->TerminalId, -1, call->epId, NULL);				// Ignore error since it would trigger a loop
			free_call_slot(call);
			break;

		case F19_RELEASE_PEND:
			release_req(call, API_RR_NORMAL);
			break;

		default:
			printf("Error, invalid state in %s\n", __FUNCTION__);
			res = -1;
			break;
	}

	if(res) release_req(call, API_RR_UNKNOWN);

	free(msgConRes);
	free(buf);

	return res;
}



//-------------------------------------------------------------
// Asterisk has replied to a ubus call we made
int asterisk_cfm(int pcmId, int err) {
	ApiFpCcConnectReqType *msgConReq;
	ApiCallStatusType status;
	struct call_t *call;
	rsuint16 bufLen;
	rsuint8 *buf;

	call = NULL;
	buf = NULL;
	bufLen = 0;

	// Sanity check data from Asterisk
	if(pcmId >= 0 && pcmId < MAX_NR_PCM) {
		call = find_call_by_endpoint_id(pcmId);
		if(!call) return -1;
	}

	if(err) {
		printf("NACK from Asterisk, terminating call\n");
		if(call) return release_req(call, API_RR_INSUFFICIENT_RESOURCES);
	}

	if(!call || pcmId < 0 || pcmId > MAX_NR_PCM - 1) {
		printf("Warning, invalid PCM %d\n", pcmId);
		return -1;
	}

	if(fill_info_elements(call, &buf, &bufLen)) return -1;

	// Send "call should become connected"
	status.CallStatusSubId = API_SUB_CALL_STATUS;
	status.CallStatusValue.State = API_CSS_CALL_CONNECT;
	ApiBuildInfoElement((ApiInfoElementType**) &buf, &bufLen,
		API_IE_CALL_STATUS, sizeof(status), (rsuint8*) &status);

	msgConReq = malloc(sizeof(ApiFpCcConnectReqType) - 1 + bufLen);
	msgConReq->Primitive = API_FP_CC_CONNECT_REQ;
	msgConReq->CallReference = call->CallReference;
	msgConReq->InfoElementLength = bufLen;

	switch(call->state) {
		case F01_INITIATED:
			printf("API_FP_CC_CONNECT_REQ\n");
			memcpy(msgConReq->InfoElement, buf, bufLen);
			mailProto.send(dect_bus, (uint8_t*) msgConReq,
				sizeof(ApiFpCcConnectReqType) - 1 + bufLen);
			break;

		case Q22_CC_RELEASE_COM:
			free_call_slot(call);
			break;

		case F19_RELEASE_PEND:
		case F10_ACTIVE:
		case F06_PRESENT:
		case F00_NULL:
			break;

		default:
			printf("Error, invalid state in %s\n", __FUNCTION__);
			release_req(call, API_RR_UNKNOWN);
			break;
	}

	free(msgConReq);
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
						call->SystemCallId.ApiSystemCallId = sysId->ApiSystemCallId;
						call->SystemCallId.ApiSubId = API_SUB_CALL_ID;
						printf("API_IE_SYSTEM_CALL_ID %d\n",
							call->SystemCallId.ApiSystemCallId);
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



//-------------------------------------------------------------
// Create a list of embedded info elements for next mail
// message. Don't bother to select any particula data,
// just send everything we've got that handset might
// have use for.
static int fill_info_elements(struct call_t *call, rsuint8 **buf, rsuint16 *bufLen) {
	ApiAudioDataFormatListType *pcmFormat;
	ApiCallingNumberType callerNumber;
	ApiSystemCallIdType SystemCallId;
	struct terminal_t *terminal;
	ApiCodecListType *codecs;
	ApiLineIdType line;
	int i;

	// Find the handset in our list of registereds
	for(i = 0; i < MAX_NR_HANDSETS &&
		handsets.terminal[i].id != call->TerminalId; i++);
	if(i == MAX_NR_HANDSETS) return -1;
	terminal = &handsets.terminal[i];

	// Send system call ID
	if(call->SystemCallId.ApiSubId == API_SUB_CALL_ID) {
		SystemCallId.ApiSubId = API_SUB_CALL_ID;
		SystemCallId.ApiSystemCallId = call->SystemCallId.ApiSystemCallId;
		ApiBuildInfoElement((ApiInfoElementType**) buf, bufLen,
			API_IE_SYSTEM_CALL_ID, sizeof(ApiSystemCallIdType),
			(rsuint8*) &SystemCallId);
	}

	line.ApiSubId = API_SUB_LINE_ID_EXT_LINE_ID;
	line.ApiLineValue.Value = 1;
	ApiBuildInfoElement((ApiInfoElementType**) buf, bufLen,
		API_IE_LINE_ID, sizeof(line), (rsuint8*) &line);

	// Send the codecs we want the handset to use
	codecs = calloc(1, sizeof(ApiCodecListType) + sizeof(ApiCodecInfoType));
	codecs->NegotiationIndicator = API_NI_POSSIBLE;
	if(call->BasicService == API_WIDEBAND_SPEECH ||
			terminal->BasicService == API_WIDEBAND_SPEECH) {
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
	ApiBuildInfoElement((ApiInfoElementType**) buf, bufLen,
		API_IE_CODEC_LIST, sizeof(ApiCodecListType) + sizeof(ApiCodecInfoType) *
		(codecs->NoOfCodecs - 1), (rsuint8*) codecs);

	// Send callers phone number
	if(call->cid) {
		callerNumber.NumberType = ANT_NATIONAL;
		callerNumber.Npi = ANPI_NATIONAL;
		callerNumber.PresentationInd = API_PRESENTATION_ALLOWED;
		callerNumber.ScreeningInd = API_USER_PROVIDED_NOT_SCREENED;
		callerNumber.NumberLength = strlen(call->cid);
		memcpy(callerNumber.Number, call->cid, strlen(call->cid));
		ApiBuildInfoElement((ApiInfoElementType**) buf, bufLen,
			API_IE_CALLING_PARTY_NUMBER, sizeof(callerNumber) +
			strlen(call->cid), (rsuint8*) &callerNumber);
	}

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
	ApiBuildInfoElement((ApiInfoElementType**) buf, bufLen,
		API_IE_AUDIO_DATA_FORMAT, sizeof(ApiAudioDataFormatListType) +
		sizeof(ApiAudioDataFormatInfoType) * (pcmFormat->NoOfCodecs - 1),
		(rsuint8*) pcmFormat);

	free(pcmFormat);
	free(codecs);

	return 0;
}



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
			else {
				return release_req(call, API_RR_UNKNOWN);
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
				cc_state_names[call->state], cc_state_names[Q22_CC_RELEASE_COM]);
			call->state = Q22_CC_RELEASE_COM;
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
// 	ApiSystemCallIdType SystemCallId;
	ApiFpCcReleaseReqType *msgOut;
	rsuint16 bufLen;
	rsuint8 *buf;
	int res;

	res = 0;
	buf = NULL;
	bufLen = 0;
	if(fill_info_elements(call, &buf, &bufLen)) return -1;

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

	free(msgOut);

	return res;
}



//-------------------------------------------------------------
// We have been asked by outside world (Asterisk)
// to terminate a call.
int release_req_async(uint32_t termId, int pcmId) {
	struct call_t *call;

	call = find_call_by_endpoint_id(pcmId);
	if(!call) return -1;

	return release_req(call, API_RR_NORMAL);
}



//-------------------------------------------------------------
// Handset user has pressed the on hook key, we terminate the call.
static int release_ind(busmail_t *m) {
// 	ApiSystemCallIdType SystemCallId;
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
	if(fill_info_elements(call, &buf, &bufLen)) return -1;

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

	free(msgOut);

	// Request shut down of PCM audio
	return audio_format_req(call);
}



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
	
		case API_FP_CC_INFO_IND:
			res = info_ind(msgIn);
			break;
	
		case API_FP_SET_AUDIO_FORMAT_CFM:
			res = audio_format_cfm(msgIn);
			break;
	
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
		calls[i].SystemCallId.ApiSubId = API_SUB_CALL_ID_INVALID;
	}

	mailProto.add_handler(bus, external_call_mail_handler);
}

