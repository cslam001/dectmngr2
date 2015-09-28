
#include <stdio.h>

#include <Api/FpGeneral/ApiFpGeneral.h>
#include <Api/CodecList/ApiCodecList.h>
#include <Api/FpCc/ApiFpCc.h>
#include <Api/FpMm/ApiFpMm.h>
#include <Api/ProdTest/ApiProdTest.h>
#include <Api/RsStandard.h>

#include "busmail.h"

static void api_parser_handler(packet_t *p) {

	busmail_t * m = (busmail_t *) &p->data[0];

	/* Application command */
	switch (m->mail_header) {

	case API_FP_RESET_IND:
		printf("API_FP_RESET_IND\n");
		break;

	case API_LINUX_INIT_GET_SYSTEM_INFO_CFM:
		printf("API_LINUX_INIT_GET_SYSTEM_INFO_CFM\n");
		break;

	case API_LINUX_INIT_CFM:
		printf("API_FP_LINUX_INIT_CFM\n");
		break;

	case API_LINUX_NVS_UPDATE_IND:
		printf("API_FP_LINUX_NVS_UPDATE_IND\n");
		break;

	case API_PROD_TEST_CFM:
		printf("API_PROD_TEST_CFM\n");
		break;

	case RTX_EAP_HW_TEST_CFM:
		printf("RTX_EAP_HW_TEST_CFM\n");
		break;

	case API_FP_GET_FW_VERSION_CFM:
		printf("API_FP_GET_FW_VERSION_CFM\n");
		break;
	
	case API_FP_FEATURES_CFM:
		printf("API_FP_FEATURES_CFM\n");
		break;

	case API_SCL_STATUS_IND:
		printf("API_SCL_STATUS_IND\n");
		break;

	case API_FP_MM_SET_REGISTRATION_MODE_CFM:
		printf("API_FP_MM_SET_REGISTRATION_MODE_CFM\n");
		break;

	case API_FP_CC_SETUP_IND:
		printf("API_FP_CC_SETUP_IND\n");
		break;

	case API_FP_CC_SETUP_REQ:
		printf("API_FP_CC_SETUP_REQ\n");
		break;

	case API_FP_CC_RELEASE_IND:
		printf("API_FP_CC_RELEASE_IND\n");
		break;

	case API_FP_CC_RELEASE_CFM:
		printf("API_FP_CC_RELEASE_CFM\n");
		break;

	case API_FP_CC_SETUP_CFM:
		printf("API_FP_CC_SETUP_CFM\n");
		break;

	case API_FP_CC_REJECT_IND:
		printf("API_FP_CC_REJECT_IND\n");
		break;

	case API_FP_CC_CONNECT_IND:
		printf("API_FP_CC_CONNECT_IND\n");
		break;
		
	case API_FP_CC_CONNECT_CFM:
		printf("API_FP_CC_CONNECT_CFM\n");
		break;
		
	case API_FP_CC_ALERT_IND:
		printf("API_FP_CC_ALERT_IND\n");
		break;
		
	case API_FP_CC_ALERT_CFM:
		printf("API_FP_CC_ALERT_CFM\n");
		break;
		
	case API_FP_CC_SETUP_ACK_CFM:
		printf("API_FP_CC_SETUP_ACK_CFM\n");
		break;
		
	case API_FP_CC_INFO_IND:
		printf("API_FP_CC_INFO_IND\n");
		break;

	case API_FP_INIT_PCM_CFM:
		printf("API_FP_INIT_PCM_CFM\n");
		break;

	case API_FP_SET_AUDIO_FORMAT_CFM:
		printf("API_FP_SET_AUDIO_FORMAT_CFM\n");
		break;

	case API_FP_CC_CALL_PROC_CFM:
		printf("API_FP_CC_CALL_PROC_CFM\n");
		break;

	default:
		break;
	}
}



void api_parser_init(void * bus) {
	mailProto.add_handler(bus, api_parser_handler);
}
