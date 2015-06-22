#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>

#include <Api/FpGeneral/ApiFpGeneral.h>
#include <Api/CodecList/ApiCodecList.h>
#include <Api/FpCc/ApiFpCc.h>
#include <Api/FpMm/ApiFpMm.h>
#include <Api/ProdTest/ApiProdTest.h>
#include <Api/RsStandard.h>
#include <termios.h>

#include "dect.h"
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
#define BUF_SIZE 50000

buffer_t * buf;
static int reset_ind = 0;
int client_connected;
void * dect_bus;
void * dect_stream, * listen_stream;
void * client_stream;
int epoll_fd;
void * client_list;
void * client_bus;
void * client_list;
void * event_base;
struct sigaction act;


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

static void list_connected(int fd) {
	printf("connected fd:s : %d\n", fd);
}


void sighandler(int signum, siginfo_t * info, void * ptr) {

	printf("Recieved signal %d\n", signum);
}


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


void eap(packet_t *p) {
	
	int i;

	printf("send to dect_bus\n");
	packet_dump(p);
	
	busmail_send0(dect_bus, &p->data[3], p->size - 3);
	
	/* /\* For RSX *\/ */
	/* busmail_send_prog(dect_bus, &p->data[3], p->size - 3, 0x81); */
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
	busmail_send(dect_bus, (uint8_t *)&res, sizeof(res));

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
	busmail_send(dect_bus, (uint8_t *)&res, sizeof(res));


	ApiFpCcReleaseReqType req = {
		.Primitive = API_FP_CC_RELEASE_REQ,
		.CallReference = terminate_call,
		.Reason = API_RR_NORMAL,
		.InfoElementLength = 0,
	};
	
	printf("API_FP_CC_RELEASE_REQ\n");
	busmail_send(dect_bus, (uint8_t *)&req, sizeof(req));

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
	busmail_send(dect_bus, (uint8_t *)&res, sizeof(ApiFpCcSetupResType));


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
	busmail_send(dect_bus, (uint8_t *)req, sizeof(ApiFpCcSetupReqType) - 1 + ie_block_len);
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


static void busmail_init(packet_t *p) {
	
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

		case API_FP_CC_RELEASE_CFM:
			printf("API_FP_CC_RELEASE_CFM\n");
			release_cfm(m);
			break;

		case API_FP_CC_SETUP_CFM:
			printf("API_FP_CC_SETUP_CFM\n");
			setup_cfm(m);
			break;

		case API_FP_CC_REJECT_IND:
			printf("API_FP_CC_REJECT_IND\n");
			reject_ind(m);
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

		case API_FP_CC_INFO_IND:
			printf("API_FP_CC_INFO_IND\n");
			info_ind(m);
			break;

		}
	}
}



static void client_handler(void * client_stream, void * event) {

	int client_fd = stream_get_fd(client_stream);

	/* Client connection */
	
	if ( event_count(event) == -1 ) {
					
		perror("recv");
	} else if ( event_count(event) == 0 ) {

		event_base_delete_stream(event_base, client_stream);
		
		/* Client connection closed */
		printf("client closed connection\n");
		if (close(client_fd) == -1) {
			exit_failure("close");
		}
					
		list_delete(client_list, client_fd);
		//list_each(client_list, list_connected);

		/* Destroy client connection object here */

	} else {

		/* Data is read from client */
		util_dump(event_data(event), event_count(event), "[CLIENT]");

		/* Send packets from clients to dect_bus */
		eap_write(client_bus, event);
		eap_dispatch(client_bus);

	}

}



static void listen_handler(void * listen_stream, void * event) {

	int client_fd;
	void * client_stream;
	struct sockaddr_in my_addr, peer_addr;
	socklen_t peer_addr_size;


	peer_addr_size = sizeof(peer_addr);

	if ( (client_fd = accept(stream_get_fd(listen_stream), (struct sockaddr *) &peer_addr, &peer_addr_size)) == -1) {
		exit_failure("accept");
	} else {

		printf("accepted connection: %d\n", client_fd);

		/* Setup stream object */
		client_stream = stream_new(client_fd);
		stream_add_handler(client_stream, client_handler);

		/* Add client_stream to event dispatcher */
		event_base_add_stream(event_base, client_stream);

		/* Add client */
		list_add(client_list, client_fd);
		//list_each(client_list, list_connected);

		/* Setup client busmail connection */
		printf("setup client_bus\n");
		client_bus = eap_new(client_fd, eap);
		client_connected = 1;
	}
}



static int setup_listener(void) {

	int listen_fd, opt = 1;
	struct sockaddr_in my_addr, peer_addr;
	socklen_t peer_addr_size;
 
	memset(&my_addr, 0, sizeof(my_addr));
	my_addr.sin_family = AF_INET;
	my_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	my_addr.sin_port = htons(10468);
	
	if ( (listen_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1 ) {
		exit_failure("socket");
	}

	if ( (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &(opt), sizeof(opt))) == -1 ) {
		exit_failure("setsockopt");
	}

	if ( (bind(listen_fd, (struct sockaddr *) &my_addr, sizeof(struct sockaddr))) == -1) {
		exit_failure("bind");
	}
	
	if ( (listen(listen_fd, MAX_LISTENERS)) == -1 ) {
		exit_failure("bind");
	}


	return listen_fd;
}


void dect_handler(void * dect_stream, void * event) {


	/* Add input to busmail subsystem */
	if (busmail_write(dect_bus, event) < 0) {
		printf("busmail buffer full\n");
	}
	

	/* Process whole packets in buffer. The previously registered
	   callback will be called for application frames */
	busmail_dispatch(dect_bus);
}


static void setup_signal_handler(void) {

	/* Setup signal handler. When writing data to a
	   client that closed the connection we get a 
	   SIGPIPE. We need to catch it to avoid being killed */
	memset(&act, 0, sizeof(act));
	act.sa_sigaction = sighandler;
	act.sa_flags = SA_SIGINFO;
	sigaction(SIGPIPE, &act, NULL);
}


void init_app_state(void * event_b, config_t * config) {
	
	int dect_fd, listen_fd;

	printf("APP_STATE\n");
	event_base = event_b;

	/* Setup dect tty */
	dect_fd = tty_open("/dev/ttyS1");
	tty_set_raw(dect_fd);
	tty_set_baud(dect_fd, B115200);
	

	/* Register dect stream */
	dect_stream = stream_new(dect_fd);
	stream_add_handler(dect_stream, dect_handler);
	event_base_add_stream(event_base, dect_stream);


	dect_conf_init();


	/* Init busmail subsystem */
	dect_bus = busmail_new(dect_fd);

	/* Add handlers */
	busmail_add_handler(dect_bus, busmail_init);
	/* busmail_add_handler(dect_bus, internal_call); */


	/* Init client list */
	client_list = list_new();

	/* Setup listening socket */
	listen_fd = setup_listener();
	listen_stream = stream_new(listen_fd);
	stream_add_handler(listen_stream, listen_handler);
	event_base_add_stream(event_base, listen_stream);



	/* Connect and reset dect chip */
	printf("DECT TX TO BRCM RX\n");
	if(gpio_control(118, 0)) return;
 	printf("RESET_DECT\n");
	if(dect_chip_reset()) return;


	return;
}


void handle_app_package(void * event) {

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





struct state_handler app_handler = {
	.state = APP_STATE,
	.init_state = init_app_state,
	.event_handler = handle_app_package,
};

struct state_handler * app_state = &app_handler;
