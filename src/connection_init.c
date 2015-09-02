#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/timerfd.h>

#include "connection_init.h"
#include "busmail.h"
#include "util.h"
#include "ubus.h"
#include "error.h"
#include "stream.h"
#include "event_base.h"

#include <Api/FpGeneral/ApiFpGeneral.h>
#include <Api/CodecList/ApiCodecList.h>
#include <Api/FpCc/ApiFpCc.h>
#include <Api/FpMm/ApiFpMm.h>
#include <Api/ProdTest/ApiProdTest.h>
#include <Api/FpAudio/ApiFpAudio.h>
#include <Api/RsStandard.h>

#define TRUE 1
#define FALSE 0

enum remote_bool_t {
	UNKNOWN,
	PENDING_ACTIVE,										// We have sent a request to activate something
	ACTIVE,												// Confirmed response, it's active
	PENDING_INACTIVE,
	INACTIVE,
};

static const char *remote_bool_str[] = {				// String explanations of enum remote_bool_t
	"unknown",
	"pending active",
	"active",
	"pending inactive",
	"inactive",
};


struct connection_t {
	enum remote_bool_t registration;
	enum remote_bool_t radio;
};


static int reset_ind = 0;
void * dect_bus;
static struct connection_t connection;
static int timer_fd;
static void *timer_stream;



//-------------------------------------------------------------
static void fw_version_cfm(busmail_t *m) {

	ApiFpGetFwVersionCfmType * p = (ApiFpGetFwVersionCfmType *) &m->mail_header;

	printf("fw_version_cfm\n");
	
	if (p->Status == RSS_SUCCESS) {
		printf("Status: RSS_SUCCESS\n");
	} else {
		printf("Status: RSS_FAIL: %x\n", p->Status);
	}

	printf("VersionHex %x\n", (unsigned int)p->VersionHex);
	
	if (p->DectType == API_EU_DECT) {
		printf("DectType: API_EU_DECT\n");
	} else {
		printf("DectType: BOGUS\n");
	}

	return;
}


//-------------------------------------------------------------
int connection_get_status(char **keys, char **values) {

	*keys = calloc(1, 100);
	if(!*keys) return -1;
	*values = calloc(1, 100);
	if(!*values) return -1;

	strcat(*keys, "radio\n");
	strcat(*values, remote_bool_str[connection.radio]);
	strcat(*values, "\n");
	strcat(*keys, "registration\n");
	strcat(*values, remote_bool_str[connection.registration]);
	strcat(*values, "\n");


	return 0;
}



//-------------------------------------------------------------
static void connection_init_handler(packet_t *p) {
	busmail_t * m = (busmail_t *) &p->data[0];

	if (m->task_id != 1) return;
	
	/* Application command */
	switch (m->mail_header) {
		
	case API_FP_RESET_IND:
		if (reset_ind == 0) {
			reset_ind = 1;
			connection.radio = INACTIVE;
			ubus_send_string("radio", ubusStrInActive);

			printf("\nWRITE: API_FP_GET_FW_VERSION_REQ\n");
			ApiFpGetFwVersionReqType m1 = { .Primitive = API_FP_GET_FW_VERSION_REQ, };
			busmail_send(dect_bus, (uint8_t *)&m1, sizeof(ApiFpGetFwVersionReqType));

		}
		break;

	case API_FP_GET_FW_VERSION_CFM:
		{
			/* Setup terminal id */
			printf("\nWRITE: API_FP_FEATURES_REQ\n");
			ApiFpCcFeaturesReqType fr = { .Primitive = API_FP_FEATURES_REQ,
				.ApiFpCcFeature = API_FP_CC_EXTENDED_TERMINAL_ID_SUPPORT };
	
			busmail_send(dect_bus, (uint8_t *)&fr, sizeof(ApiFpCcFeaturesReqType));
		}
		break;

	case API_FP_FEATURES_CFM:
		{
			/* Init PCM bus */
			printf("\nWRITE: API_FP_INIT_PCM_REQ\n");
			ApiFpInitPcmReqType pcm_req =  { .Primitive = API_FP_INIT_PCM_REQ,
						 .PcmEnable = 0x1,
						 .IsMaster = 0x0,
						 .DoClockSync = 0x1,
						 .PcmFscFreq = AP_FSC_FREQ_8KHZ,  // PCM FS 16/8 Khz select (1 - 16Khz, 0 - 8Khz)
						 .PcmFscLength = AP_FSC_LENGTH_NORMAL,
						 .PcmFscStartAligned = 0x0,
						 .PcmClk = 0x0,    /* Ignored if device is slave */
						 .PcmClkOnRising = 0x0,
						 .PcmClksPerBit = 0x1,
						 .PcmFscInvert = 0x0,
						 .PcmCh0Delay = 0x0,
						 .PcmDoutIsOpenDrain = 0x1, /* Must be 1 if mult. devices on bus */
						 .PcmIsOpenDrain = 0x0,  /* 0 == Normal mode */
			};
			busmail_send(dect_bus, (uint8_t *)&pcm_req, sizeof(ApiFpInitPcmReqType));
		}
		break;

	case API_FP_INIT_PCM_CFM:
		{
			ApiFpInitPcmCfmType * resp = (ApiFpInitPcmCfmType *) &m->mail_header;
			print_status(resp->Status);

			ubus_send_string("dectmngr", ubusStrActive);

			/* Start protocol */
			connection_set_radio(1);
		}
		break;

	case API_FP_MM_SET_REGISTRATION_MODE_CFM:
		{
			ApiFpMmSetRegistrationModeCfmType *resp =
				(ApiFpMmSetRegistrationModeCfmType*) &m->mail_header;
			print_status(resp->Status);

			if(resp->Status == RSS_SUCCESS) {
				if(connection.registration == PENDING_ACTIVE) {
					connection.registration = ACTIVE;
					ubus_send_string("registration", ubusStrActive);
				}
				else if(connection.registration == PENDING_INACTIVE) {
						connection.registration = INACTIVE;
						ubus_send_string("registration", ubusStrInActive);
				}
			}
		}
		break;

	case API_FP_MM_REGISTRATION_COMPLETE_IND:
		connection_set_registration(0);
		break;
	}
}




//-------------------------------------------------------------
// Timer handler, for turning of registration
// after a delay if no handset has registered.
static void timer_handler(void * dect_stream, void * event) {
	uint64_t expired;
	int res;

	expired = 1;

	res = read(timer_fd, &expired, sizeof(expired));
	if(res == -1) {
		perror("Error, reading timer fd");
	}
	else if(res != sizeof(expired)) {
		printf("Warning, short timer fd read %d\n", res);
	}

	/* Has the radio just been turned on? Then
	 * we may need to enable registration mode.
	 * Othwerwise it's time to disable it. */
	if(connection.radio == ACTIVE) {
		if(connection.registration == PENDING_ACTIVE) {
			connection_set_registration(1);
		}
		else if(connection.registration == ACTIVE) {
			connection_set_registration(0);
		}
	}
	else if(connection.radio == INACTIVE && 
			connection.registration != INACTIVE) {
		connection_set_registration(0);
	}
}



//-------------------------------------------------------------
void connection_init(void * bus) {

	dect_bus = bus;
	busmail_add_handler(bus, connection_init_handler);

	timer_fd = timerfd_create(CLOCK_MONOTONIC, O_NONBLOCK);
	if(timer_fd == -1) {
		exit_failure("Error creating handset timer");
	}

	timer_stream = stream_new(timer_fd);
	stream_add_handler(timer_stream, 0, timer_handler);
	event_base_add_stream(timer_stream);

	/* At startup of dectmngr the dect chip is
	 * reseted and thus we know initial state. */
	connection.radio = INACTIVE;
	connection.registration = INACTIVE;
}



//-------------------------------------------------------------
/* Start or stop protocol (the DECT radio) */
int connection_set_radio(int onoff) {
	struct itimerspec newTimer;

	if(onoff) {
		printf("\nWRITE: API_FP_MM_START_PROTOCOL_REQ\n");
		ApiFpMmStartProtocolReqType r =  { .Primitive = API_FP_MM_START_PROTOCOL_REQ, };
		busmail_send(dect_bus, (uint8_t *)&r, sizeof(ApiFpMmStartProtocolReqType));
		connection.radio = ACTIVE;												// No confirmation is replied
		ubus_send_string("radio", ubusStrActive);
	}
	else {
		ApiFpMmStopProtocolReqType r = { .Primitive = API_FP_MM_STOP_PROTOCOL_REQ, };
		busmail_send(dect_bus, (uint8_t *)&r, sizeof(ApiFpMmStopProtocolReqType));
		connection.radio = INACTIVE;											// No confirmation is replied
		ubus_send_string("radio", ubusStrInActive);

	}

	memset(&newTimer, 0, sizeof(newTimer));
	newTimer.it_value.tv_sec = 1;
	if(timerfd_settime(timer_fd, 0, &newTimer, NULL) == -1) {
		perror("Error setting timer");
	}

	return 0;
}



//-------------------------------------------------------------
// Enable registration of phones and arm a
// timer for possible timeout.
int connection_set_registration(int onoff) {
	struct itimerspec newTimer;
	int doSendMsg;

	ApiFpMmSetRegistrationModeReqType m = {
		.Primitive = API_FP_MM_SET_REGISTRATION_MODE_REQ,
		.DeleteLastHandset = true
	};

	memset(&newTimer, 0, sizeof(newTimer));
	doSendMsg = 0;

	if(onoff) {
		connection.registration = PENDING_ACTIVE;

		if(connection.radio == ACTIVE) {
			m.RegistrationEnabled = true;
			newTimer.it_value.tv_sec = 180;
			doSendMsg = 1;
		}
		else if(connection_set_radio(1) == 0) {
			newTimer.it_value.tv_sec = 1;
		}
	}
	else {
		m.RegistrationEnabled = false;
		connection.registration = PENDING_INACTIVE;
		doSendMsg = 1;
	}

	/* Either active registration mode
	 * or do nothing if we first need to
	 * enable the radio. */
	if(doSendMsg) {
		busmail_send(dect_bus, (uint8_t *)&m,
			sizeof(ApiFpMmSetRegistrationModeReqType));
	}

	/* Activate or cancel timer for turning
	 * registration off after a delay. */
	if(timerfd_settime(timer_fd, 0, &newTimer, NULL) == -1) {
		perror("Error setting timer");
	}

	return 0;
}

