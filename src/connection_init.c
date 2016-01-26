#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/timerfd.h>
#include <sys/ioctl.h>


#include "connection_init.h"
#include "busmail.h"
#include "util.h"
#include "ubus.h"
#include "error.h"
#include "stream.h"
#include "event_base.h"
#include "nvs.h"

#include <Api/FpGeneral/ApiFpGeneral.h>
#include <Api/CodecList/ApiCodecList.h>
#include <Api/FpCc/ApiFpCc.h>
#include <Api/FpMm/ApiFpMm.h>
#include <Api/ProdTest/ApiProdTest.h>
#include <Api/FpAudio/ApiFpAudio.h>
#include <Api/RsStandard.h>
#include <Api/Linux/ApiLinux.h>
#include <dectshimdrv.h>


static int reset_ind = 0;
static void *dect_bus;
struct connection_t connection;
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

	case API_FP_RESET_IND:														// External Dect has reseted
		if (reset_ind == 0) {
			reset_ind = 1;
			connection.radio = INACTIVE;
			ubus_send_string("radio", ubusStrInActive);
			ubus_call_string("led.dect", "set", "state", "off", NULL);

			printf("WRITE: API_FP_GET_FW_VERSION_REQ\n");
			ApiFpGetFwVersionReqType m1 = { .Primitive = API_FP_GET_FW_VERSION_REQ, };
			mailProto.send(dect_bus, (uint8_t *)&m1, sizeof(ApiFpGetFwVersionReqType));
		}
		break;

	case API_LINUX_INIT_GET_SYSTEM_INFO_CFM:									// Query internal Dect. Does it need to be initialized?
		{	
			ApiLinuxInitGetSystemInfoCfmType *resp = 
				(ApiLinuxInitGetSystemInfoCfmType*) &m->mail_header;

			if(resp->NvsSize != DECT_NVS_SIZE) {
				exit_failure("Invalid NVS size from driver, something is wrong!\n");
			}
			else if(resp->MaxMailSize == 0) {
				/* Internal Dect has already been initialized previously
				 * and we MUST only do the init once, so skip it. */
				printf("WRITE: API_FP_FEATURES_REQ\n");
				ApiFpCcFeaturesReqType req = {
					.Primitive = API_FP_FEATURES_REQ,
					.ApiFpCcFeature = API_FP_CC_EXTENDED_TERMINAL_ID_SUPPORT
				};

				mailProto.send(dect_bus, (uint8_t *) &req, sizeof(req));
			}
			else {
				// Initialization is needed, send NVS data to kernel driver
				ApiLinuxInitReqType *req = (ApiLinuxInitReqType*)
					malloc(sizeof(ApiLinuxInitReqType) + DECT_NVS_SIZE);
				memset(req, 0, sizeof(ApiLinuxInitReqType) + DECT_NVS_SIZE);
				req->Primitive = API_LINUX_INIT_REQ;
	
				if(nvs_file_read((uint32_t*) &req->LengthOfData, 
						req->Data) == 0) {
					mailProto.send(dect_bus, (uint8_t*) req,
						sizeof(ApiLinuxInitReqType) + req->LengthOfData - 1);
				}
				free(req);
			}
		}
		break;

	case API_LINUX_NVS_UPDATE_IND:												// Internal Dect writes to the NVS-file
		{
			ApiLinuxNvsUpdateIndType *resp =
				(ApiLinuxNvsUpdateIndType*) &m->mail_header;
			nvs_file_write(resp->NvsOffset, resp->NvsDataLength, resp->NvsData);
		}
		break;

	case API_LINUX_INTERNAL_ERROR_IND:
		{
			ApiLinuxInternalErrorIndType *resp =
				(ApiLinuxInternalErrorIndType*) &m->mail_header;
			printf("Warning, internal Dect error %d\n", resp->ErrorCode);
		}
		break;	

	case API_FP_GET_FW_VERSION_CFM:												// External Dect has initialized
		fw_version_cfm(m);
		// fall through
	case API_LINUX_INIT_CFM:													// Internal Dect has initialized
		{
			/* Setup terminal id */
			printf("WRITE: API_FP_FEATURES_REQ\n");
			ApiFpCcFeaturesReqType req = {
				.Primitive = API_FP_FEATURES_REQ,
				.ApiFpCcFeature = API_FP_CC_EXTENDED_TERMINAL_ID_SUPPORT
			};

			mailProto.send(dect_bus, (uint8_t *) &req, sizeof(req));
		}
		break;

	case API_FP_FEATURES_CFM:
		{
			/* Init PCM bus */
			printf("WRITE: API_FP_INIT_PCM_REQ\n");
			ApiFpInitPcmReqType pcm_req =  { .Primitive = API_FP_INIT_PCM_REQ,
							 .PcmEnable = 0x1,
							 .IsMaster = 0x0,
							 .DoClockSync = 0x1,
							 .PcmFscFreq = AP_FSC_FREQ_8KHZ,  /* FSC 8 kHz */
							 .PcmFscLength = AP_FSC_LENGTH_NORMAL,
							 .PcmFscStartAligned = 0x0, /* FSC starts one clock before data */
							 .PcmClk = 0x0,    /* Ignored if device is slave */
							 .PcmClkOnRising = 0x1, /* Data is clocked out on rising edge */
							 .PcmClksPerBit = 0x1, /* One clock per bit */
							 .PcmFscInvert = 0x0,  /* FSC not inverted */
							 .PcmCh0Delay = 0x0,   /* No 8-bit offset for chan 0 */
							 .PcmDoutIsOpenDrain = 0x1, /* Must be 1 if mult. devices on bus */
							 .PcmIsOpenDrain = 0x0,  /* 0 == Normal mode */
			};
			mailProto.send(dect_bus, (uint8_t *)&pcm_req, sizeof(ApiFpInitPcmReqType));
		}
		break;

	case API_FP_INIT_PCM_CFM:
		{
			ApiFpInitPcmCfmType * resp = (ApiFpInitPcmCfmType *) &m->mail_header;
			print_status(resp->Status);

			ubus_send_string("dectmngr", ubusStrActive);

			/* Start protocol (the radio) if
			 * it's configured by user. */
			if(uci_call_query("radio")) list_handsets();
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
					printf("Registration is active\n");
					ubus_send_string("registration", ubusStrActive);			// Send ubus event
					ubus_call_string("led.dect", "set", "state", "notice", NULL);// Light up box LED
				}
				else if(connection.registration == PENDING_INACTIVE) {
						connection.registration = INACTIVE;
						printf("Registration is inactive\n");
						ubus_send_string("registration", ubusStrInActive);
						ubus_call_string("led.dect", "set", "state", 
							(connection.radio == ACTIVE) ? "ok" : "off", NULL);
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
static void timer_handler(void *unused1, void *unused2) {
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
	mailProto.add_handler(bus, connection_init_handler);

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
	connection.uciRadioConf = RADIO_AUTO;
}



//-------------------------------------------------------------
/* Start or stop protocol (the DECT radio) */
int connection_set_radio(int onoff) {
	struct itimerspec newTimer;

	if(onoff) {
		/* Don't enable radio again if it's already active
		 * since it would inactivate registration in the
		 * timer handler. */
		if(connection.radio == ACTIVE) return 0;

		ApiFpMmStartProtocolReqType r =  { .Primitive = API_FP_MM_START_PROTOCOL_REQ, };
		mailProto.send(dect_bus, (uint8_t *)&r, sizeof(ApiFpMmStartProtocolReqType));
		connection.radio = ACTIVE;												// No confirmation is replied
		printf("Radio is active\n");
		ubus_send_string("radio", ubusStrActive);
		ubus_call_string("led.dect", "set", "state", "ok", NULL);
	}
	else {
		ApiFpMmStopProtocolReqType r = { .Primitive = API_FP_MM_STOP_PROTOCOL_REQ, };
		mailProto.send(dect_bus, (uint8_t *)&r, sizeof(ApiFpMmStopProtocolReqType));
		connection.radio = INACTIVE;											// No confirmation is replied
		printf("Radio is inactive\n");
		ubus_send_string("radio", ubusStrInActive);
		ubus_call_string("led.dect", "set", "state", "off", NULL);
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
		mailProto.send(dect_bus, (uint8_t *)&m,
			sizeof(ApiFpMmSetRegistrationModeReqType));
	}

	/* Activate or cancel timer for turning
	 * registration off after a delay. */
	if(timerfd_settime(timer_fd, 0, &newTimer, NULL) == -1) {
		perror("Error setting timer");
	}

	return 0;
}



//-------------------------------------------------------------
// Initialize SoC internal Dect
int start_internal_dect(void) {
	ApiLinuxInitGetSystemInfoReqType req;
	DECTSHIMDRV_INIT_PARAM parm;
	int shimFd, r;

	shimFd = open("/dev/dectshim", O_RDWR);
	if (shimFd == -1) exit_failure("%s: open error %d\n", __FUNCTION__, errno);

	memset(&parm, 0, sizeof(parm));
	r = ioctl(shimFd, DECTSHIMIOCTL_INIT_CMD, &parm);
	if (r != 0) exit_failure("%s: ioctl error %d\n", __FUNCTION__, errno);

	close(shimFd);

	printf("WRITE: API_LINUX_INIT_GET_SYSTEM_INFO_REQ\n");
	req.Primitive = API_LINUX_INIT_GET_SYSTEM_INFO_REQ;
	mailProto.send(dect_bus, (uint8_t *) &req, sizeof(req));

	return 0;
}



//-------------------------------------------------------------
// If user has set the radio to "auto" we will disable
// it when no handsets are registered (to reduce EMC).
int perhaps_disable_radio(void) {
	if(connection.registration == ACTIVE) return 0;

	if(connection.uciRadioConf == RADIO_AUTO) {
		return connection_set_radio(handsets.termCount > 0);
	}
	else if(connection.uciRadioConf == RADIO_ALWAYS_ON) {
		return connection_set_radio(1);
	}
	else if(connection.uciRadioConf == RADIO_ALWAYS_OFF) {
		return connection_set_radio(0);
	}

	return 0;
}


