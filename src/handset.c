#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "handset.h"
#include "busmail.h"
#include "util.h"
#include "ubus.h"
#include "app.h"
#include "stream.h"
#include "event_base.h"
#include "error.h"

#include <Api/FpGeneral/ApiFpGeneral.h>
#include <Api/CodecList/ApiCodecList.h>
#include <Api/FpCc/ApiFpCc.h>
#include <Api/FpMm/ApiFpMm.h>
#include <Api/ProdTest/ApiProdTest.h>
#include <Api/FpAudio/ApiFpAudio.h>
#include <Api/RsStandard.h>



static struct handsets_t handsets;
static void *dect_bus;




//-------------------------------------------------------------
// Query stack for one registered handset IPUI
static void get_handset_ipui(int handsetId)
{
	ApiFpMmGetHandsetIpuiReqType m = {
		.Primitive = API_FP_MM_GET_HANDSET_IPUI_REQ,
		.TerminalId = handsetId,
	};

	mailProto.send(dect_bus, (uint8_t*) &m, sizeof(m));
}



//-------------------------------------------------------------
// Dect stack responded with the IPUI
// of one registered handset.
static void got_handset_ipui(busmail_t *m)
{
	ApiFpMmGetHandsetIpuiCfmType *resp;
	int i;

	resp = (ApiFpMmGetHandsetIpuiCfmType*) &m->mail_header;

	// Sanity check
	if(resp->Status != RSS_SUCCESS) {
		printf("Error; %s() status %d\n", __func__, resp->Status);
		return;
	}

	/* Search for the handset ID in an our list
	 * for the terminal we just got the ipui for. */
	for(i = 0; i < MAX_NR_HANDSETS &&
			handsets.terminal[i].id != resp->TerminalId; i++);

	memcpy(handsets.terminal[i].ipui, resp->IPUI,
		sizeof(handsets.terminal[0].ipui));

	/* Query next handset if we yet
	 * haven't got them all. */
	i++;
	if(i >= handsets.termCount) {
//		perhaps_disable_protocol();
		ubus_reply_handset_list(0, &handsets);
	}
	else {
		get_handset_ipui(handsets.terminal[i].id);
	}
}



//-------------------------------------------------------------
// Query Dect stack for what handsets are registered
int list_handsets(void)
{
	ApiFpMmGetRegistrationCountReqType m = {
		.Primitive = API_FP_MM_GET_REGISTRATION_COUNT_REQ,
		.StartTerminalId = 0
	};

	mailProto.send(dect_bus, (uint8_t*) &m, sizeof(m));

	return 0;
}



//-------------------------------------------------------------
// Dect stack responded whith current
// number of registered handsets.
static void got_registration_count(busmail_t *m)
{
	ApiFpMmGetRegistrationCountCfmType *resp;
	int i;
	
	resp = (ApiFpMmGetRegistrationCountCfmType*) &m->mail_header;
	
	// Sanity check
	if(resp->Status != RSS_SUCCESS) {
		printf("Error; %s() status %d\n", __func__, resp->Status);
		return;
	}
	if(resp->TerminalIdCount > resp->MaxNoHandsets) return;

	handsets.termCount = resp->TerminalIdCount;
	memset(handsets.terminal, 0, sizeof(struct terminal_t) * MAX_NR_HANDSETS);

	for(i = 0 ; i < resp->TerminalIdCount; i++) {
		handsets.terminal[i].id = resp->TerminalId[i];
	}

	/* Get the ipui of the first handset. For some damn
	 * reason we can't to all of them at once. */
	if (handsets.termCount > 0) {
		get_handset_ipui(handsets.terminal[0].id);
	}
	else {
		//perhaps_disable_protocol();
		ubus_reply_handset_list(0, &handsets);
	}
}



//-------------------------------------------------------------
// Delete a registered handset from Dect stack
int delete_handset(int id) {  
	ApiFpMmDeleteRegistrationReqType m = {
		.Primitive = API_FP_MM_DELETE_REGISTRATION_REQ,
		.TerminalId = id
	};

	mailProto.send(dect_bus, (uint8_t*) &m, sizeof(m));

	return 0;
}


//-------------------------------------------------------------
// Make ALL handsets ring. To make only one of
// them ring Asterisk has to be used, due to such
// a case works like a phone call even though there
// is no audio.
int page_all_handsets(void) {
	ApiFpMmAlertBroadcastReqType m = {
		.Primitive = API_FP_MM_ALERT_BROADCAST_REQ,
		.Signal = API_CC_SIGNAL_ALERT_ON_PATTERN_2
	};

	mailProto.send(dect_bus, (uint8_t*) &m, sizeof(m));

	return 0;
}



//-------------------------------------------------------------
// Handle PP events such as registration
static void handset_handler(packet_t *p)
{
	busmail_t * m = (busmail_t *) &p->data[0];
	ApiFpMmGetIdCfmType *resp = (ApiFpMmGetIdCfmType*) &m->mail_header;			// For get in "Status" of those structs who has it

	if (m->task_id != 1) return;
	
	/* Application command */
	switch (m->mail_header) {
		case API_FP_MM_HANDSET_PRESENT_IND:
			ubus_send_string("handset", "present");
			break;

		case API_FP_MM_GET_REGISTRATION_COUNT_CFM:
			got_registration_count(m);
			break;

		case API_FP_MM_GET_HANDSET_IPUI_CFM:
			got_handset_ipui(m);
			break;

		case API_FP_MM_REGISTRATION_COMPLETE_IND:
			if(resp->Status == RSS_SUCCESS) {
				ubus_send_string("handset", "add");
			}
			break;

		case API_FP_MM_DELETE_REGISTRATION_CFM:
			if(resp->Status == RSS_SUCCESS) {
				ubus_send_string("handset", "remove");
			}
			break;
	}
}




//-------------------------------------------------------------
void handset_init(void * bus)
{
	dect_bus = bus;
	mailProto.add_handler(bus, handset_handler);
}


