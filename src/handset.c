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

#include <Api/FpGeneral/ApiFpGeneral.h>
#include <Api/CodecList/ApiCodecList.h>
#include <Api/FpCc/ApiFpCc.h>
#include <Api/FpMm/ApiFpMm.h>
#include <Api/ProdTest/ApiProdTest.h>
#include <Api/FpAudio/ApiFpAudio.h>
#include <Api/RsStandard.h>



static struct handsets_t handsets;





//-------------------------------------------------------------
static void get_handset_ipui(int handsetId)
{
	ApiFpMmGetHandsetIpuiReqType m = {
		.Primitive = API_FP_MM_GET_HANDSET_IPUI_REQ,
		.TerminalId = handsetId,
	};

	busmail_send(dect_bus, (uint8_t*) &m, sizeof(m));
}



//-------------------------------------------------------------
static void got_handset_ipui(busmail_t *m)
{
	ApiFpMmGetHandsetIpuiCfmType *resp;
	int i;

	resp = (ApiFpMmGetHandsetIpuiCfmType*) &m->mail_header;

	// Sanity check
	if(resp->Status != RSS_SUCCESS) return;
	if(resp->TerminalId > handsets.termCount) return;
	printf("ipui for %d\n", resp->TerminalId);

	/* Search for the handset ID in an our list
	 * for the terminal we just got the ipui for. */
	for(i = 0; i < handsets.termCount &&
			handsets.terminal[i].id != resp->TerminalId; i++);
	if(i == handsets.termCount) return;


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
int list_handsets(void)
{
	ApiFpMmGetRegistrationCountReqType m = {
		.Primitive = API_FP_MM_GET_REGISTRATION_COUNT_REQ,
		.StartTerminalId = 0
	};

	busmail_send(dect_bus, (uint8_t*) &m, sizeof(m));

	return 0;
}



//-------------------------------------------------------------
static void got_registration_count(busmail_t *m)
{
	ApiFpMmGetRegistrationCountCfmType *resp;
	int i;
	
	resp = (ApiFpMmGetRegistrationCountCfmType*) &m->mail_header;
	
	// Sanity check
	if(resp->Status != RSS_SUCCESS) return;
	if(resp->TerminalIdCount > resp->MaxNoHandsets) return;
	printf("Max Number of Handset allowed: %d\n", resp->MaxNoHandsets);

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
	}
}



//-------------------------------------------------------------
static void handset_handler(packet_t *p)
{
	
	int i;
	busmail_t * m = (busmail_t *) &p->data[0];

	if (m->task_id != 1) return;
	
	/* Application command */
	switch (m->mail_header) {
		case API_FP_MM_HANDSET_PRESENT_IND:
		{
			ApiFpMmHandsetPresentIndType *resp =
				(ApiFpMmHandsetPresentIndType*) &m->mail_header;
			ubus_send_string("handset", "present");
		}
		break;

		case API_FP_MM_GET_REGISTRATION_COUNT_CFM:
			got_registration_count(m);
			break;

		case API_FP_MM_GET_HANDSET_IPUI_CFM:
			got_handset_ipui(m);
			break;

	}
}



//-------------------------------------------------------------
void handset_init(void * bus)
{
	busmail_add_handler(bus, handset_handler);
}


