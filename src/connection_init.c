#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/timerfd.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "connection_init.h"
#include "busmail.h"
#include "util.h"
#include "ubus.h"
#include "error.h"
#include "stream.h"
#include "event_base.h"
#include "nvs.h"
#include "app.h"
#include "rawmailproxy.h"

#include <Api/FpGeneral/ApiFpGeneral.h>
#include <Api/CodecList/ApiCodecList.h>
#include <Api/FpCc/ApiFpCc.h>
#include <Api/FpMm/ApiFpMm.h>
#include <Api/ProdTest/ApiProdTest.h>
#include <Api/FpAudio/ApiFpAudio.h>
#include <Api/RsStandard.h>
#include <Api/Linux/ApiLinux.h>
#include <Api/FpUle/ApiFpUle.h>
#include "PtCmdDef.h"
#include <dectshimdrv.h>


/* Backwards compatibility between Natalie version.
 * The API differs between versions. However, it does NOT
 * provade a method for discovering this at compile
 * time! Thus we don't know what we link agains until
 * it's to late... Below is an ugly workaround. */
#if ROS_PRIMITIVE_COUNT >= 1304													// Natalie v12.26
#define API_FP_CC_FEATURES_REQ	(API_FP_CC_SET_FEATURES_REQ)
#define API_FP_CC_FEATURES_CFM	(API_FP_CC_SET_FEATURES_CFM)
typedef ApiFpCcSetFeaturesReqType ApiFpCcFeaturesReqType;
#elif ROS_PRIMITIVE_COUNT >= 1269												// Natalie v11.19
#else
#error Error, unknown Natalie version
#endif


//-------------------------------------------------------------
static const char strTypeUnknown[] = "unknown";
static const char strTypeInval[] = "invalid";

static const char *remote_bool_str[] = {										// String explanations of enum remote_bool_t
	"unknown",
	"pending active",
	"active",
	"pending inactive",
	"inactive",
};

static const uint8_t invalidRfpis[][5] = {										// List of invalid RFPIs in FP
	{ 0, 0, 0, 0, 0, },															// Null
	{ 0xffu, 0xffu, 0xffu, 0xffu, 0xffu },										// Empty flash
	{ 0x02u, 0x3fu, 0x90u, 0x00, 0xf8u }										// Duplicated address (several DG400 devices had the same)
};

static const uint8_t invalidRfpiRanges[][3] = {									// List of blacklisted RFPI ranges (first three bytes of full RFPI)
	{ 0x02u, 0xc3u, 0x55u },													// Early pilot run of DG400
	{ 0x02u, 0x3fu, 0x88u }														// Bad example from produciton document DG400
};



//-------------------------------------------------------------
static time_t reset_ind = 0;
static void *dect_bus;
struct connection_t connection;
static int timer_fd;
static void *timer_stream;



//-------------------------------------------------------------
static void fw_version_cfm(busmail_t *m) {
	const char strType[] = "DectType";
	ApiFpGetFwVersionCfmType * p = (ApiFpGetFwVersionCfmType *) &m->mail_header;

	printf("fw_version_cfm\n");
	
	if (p->Status == RSS_SUCCESS) {
		printf("Status: RSS_SUCCESS\n");
	} else {
		printf("Status: RSS_FAIL: %x\n", p->Status);
	}

	printf("VersionHex %x\n", (unsigned int)p->VersionHex);
	syslog(LOG_INFO, "Firmware version %x\n", (unsigned int) p->VersionHex);
	snprintf(connection.fwVersion, sizeof(connection.fwVersion),
		"%x", (unsigned int) p->VersionHex);
	
	switch(p->DectType) {
		case API_EU_DECT:
			printf("%s: API_EU_DECT\n", strType);
			sprintf(connection.type, "EU");
			break;
		case API_US_DECT:
			printf("%s: API_US_DECT\n", strType);
			sprintf(connection.type, "US");
			break;
		case API_SA_DECT:
			printf("%s: API_SA_DECT\n", strType);
			sprintf(connection.type, "SA");
			break;
		case API_TAIWAN_DECT:
			printf("%s: API_TAIWAN_DECT\n", strType);
			sprintf(connection.type, "Taiwan");
			break;
		case API_CHINA_DECT:
			printf("%s: API_CHINA_DECT\n", strType);
			sprintf(connection.type, "China");
			break;
		case API_THAILAND_DECT:
			printf("%s: API_THAILAND_DECT\n", strType);
			sprintf(connection.type, "Thailand");
			break;
		case API_DECT_TYPE_INVALID:
			printf("%s: %s\n", strType, strTypeInval);
			snprintf(connection.type, sizeof(strTypeInval), "%s", strTypeInval);
			break;
		default:
			printf("%s: %s\n", strType, strTypeUnknown);
			snprintf(connection.type, sizeof(strTypeUnknown),
				"%s", strTypeUnknown);
			break;
	}

	return;
}



//-------------------------------------------------------------
// Returns true if radio can start
static int is_radio_prerequisites_ok(void) {
	uint8_t nvramRfpi[5];
	unsigned i;
	int valid;

	valid = 1;
	
	// Fetch RFPI from our filesystem
	if(nvs_rfpi_patch(nvramRfpi)) valid = 0;

	// Does the RFPI in NVS look valid?
	if(memcmp(connection.rfpi, nvramRfpi, 5)) valid = 0;
	for(i = 0; i < sizeof(invalidRfpis) / 5; i++) {
		if(memcmp(invalidRfpis[i], nvramRfpi, 5) == 0) valid = 0;
	}
	for(i = 0; i < sizeof(invalidRfpiRanges) / 3; i++) {
		if(memcmp(invalidRfpiRanges[i], nvramRfpi, 3) == 0) valid = 0;
	}

	// Check Dect type (country). Has it been set?
	if(strlen(connection.type) == 0) valid = 0;
	if(strncmp(connection.type, strTypeUnknown, sizeof(connection.type)) == 0 ||
			strncmp(connection.type, strTypeInval, sizeof(connection.type) == 0)) {
		valid = 0;
	}

	if(valid) return 1;

	printf("Warning, incorrect NVS settings! Radio can't be used!\n");
	syslog(LOG_WARNING, "Warning, incorrect NVS settings! Radio can't be used!");
	return 0;
}



//-------------------------------------------------------------
// When external dect FP has an invalid RFPI we delete
// it and uploads a new one (copy from nvram). 
static int reprogram_rfpi(void) {
	const char errmsg1[] = "Invalid RFPI in nvram, won't reprogram RFPI!\n";
	const char errmsg2[] = "Invalid RFPI range in nvram, won't reprogram RFPI!\n";
	ApiProdTestReqType *req;
	PtSetIdReqType *nvsRfpi;
	uint8_t nvramRfpi[5];
	unsigned i;
	int res;

	res = 0;

	// Fetch RFPI from our filesystem
	req = malloc(sizeof(ApiProdTestReqType) - 1 + sizeof(PtSetIdReqType));
	if(!req || nvs_rfpi_patch(nvramRfpi)) {
		res = -1;
		goto out;
	}
	req->Primitive = API_PROD_TEST_REQ;
	req->Opcode = PT_CMD_SET_ID;
	req->ParameterLength = sizeof(PtSetIdReqType);
	nvsRfpi = (PtSetIdReqType*) req->Parameters;

	for(i = 0; i < sizeof(invalidRfpis) / 5; i++) {
		if(memcmp(invalidRfpis[i], nvramRfpi, 5) == 0) {
			res = -1;
			printf(errmsg1);
			syslog(LOG_WARNING, errmsg1);
			goto out;
		}
	}

	for(i = 0; i < sizeof(invalidRfpiRanges) / 3; i++) {
		if(memcmp(invalidRfpiRanges[i], nvramRfpi, 3) == 0) {
			res = -1;
			printf(errmsg2);
			syslog(LOG_WARNING, errmsg2);
			goto out;
		}
	}

	nvsRfpi->Id[0] = nvramRfpi[0];
	nvsRfpi->Id[1] = nvramRfpi[1];
	nvsRfpi->Id[2] = nvramRfpi[2];
	nvsRfpi->Id[3] = nvramRfpi[3];
	nvsRfpi->Id[4] = nvramRfpi[4];
	printf("Reprograms RFPI in NVS to: %2.2x %2.2x %2.2x %2.2x %2.2x...\n",
		nvsRfpi->Id[0], nvsRfpi->Id[1], nvsRfpi->Id[2],
		nvsRfpi->Id[3], nvsRfpi->Id[4]);

	mailProto.send(dect_bus, (uint8_t *) req,
		sizeof(ApiProdTestReqType) - 1 + sizeof(PtSetIdReqType));
out:
	free(req);
	return res;
}



//-------------------------------------------------------------
// When external dect FP has an invalid RFPI we delete
// it and uploads a new one (copy from nvram). 
static int reprogram_clock_freq(void) {
	PtSetFreqReqType *nvsFreq;
	ApiProdTestReqType *req;
	uint8_t nvramFreq[8];

	// Fetch calibrated radio frequency from our filesystem
	req = malloc(sizeof(ApiProdTestReqType) - 1 + sizeof(PtSetFreqReqType));
	if(!req || nvs_freq_patch(nvramFreq)) {
		free(req);
		return -1;
	}
	req->Primitive = API_PROD_TEST_REQ;
	req->Opcode = PT_CMD_SET_CLOCK_FREQUENCY;
	req->ParameterLength = sizeof(PtSetFreqReqType);
	nvsFreq = (PtSetFreqReqType*) req->Parameters;

	// The value is byte swaped in nvram for some strange reason...
	nvsFreq->Frequency = ((uint16_t) nvramFreq[7]) << 8 |
		(uint16_t) nvramFreq[6];
	printf("Reprograms radio calibrated freq in NVS to: %4.4x...\n",
		nvsFreq->Frequency);

	mailProto.send(dect_bus, (uint8_t *) req,
		sizeof(ApiProdTestReqType) - 1 + sizeof(PtSetFreqReqType));
	free(req);

	return 0;
}



//-------------------------------------------------------------
// Send request to Natalie to perform a warm soft reset.
int dect_warm_reset(void) {
	ApiFpResetReqType reqReset = { .Primitive = API_FP_RESET_REQ };
	struct itimerspec newTimer;
	
	/* Kill third party proxy applications. They
	 * too need to restart as we now will do.
	 * Gigaset ULE has an own watchdog and will
	 * restart by itself in 45 s (if in use). This
	 * is crude and of course we need a better API! */
	if(hasProxyClient()) {
		system("/usr/bin/killall -q uleapp");
		usleep(200000);
	}

	while(reset_ind && abs(time(NULL) - reset_ind) <= MIN_RESET_WAIT_TIME) {
		printf("Please wait for reset...\n");
		sleep(1);
	}

	printf("Radio is inactive\n");
	ubus_disable_receive();
	ubus_send_string("radio", ubusStrInActive);
	ubus_call_string("led.dect", "set", "state", "off", NULL);

	printf("Reseting stack...\n");
	mailProto.send(dect_bus, (uint8_t *) &reqReset, sizeof(ApiFpResetReqType));
	usleep(0);

	/* Start a timer for when we expect to have
	 * initialized Natalie dect. If it times out
	 * something is wrong with Natalie firmware. */
	memset(&newTimer, 0, sizeof(newTimer));
	newTimer.it_value.tv_sec = MAX_RESET_INIT_TIME;
	if(timerfd_settime(timer_fd, 0, &newTimer, NULL) == -1) {
		perror("Error setting timer");
	}

	if(hwIsInternal) exit_succes("App exit for complete stack reset...");

	return 0;
}



//-------------------------------------------------------------
int connection_get_status(char **keys, char **values) {
	const int bufLen = 200;
	*keys = calloc(1, bufLen);
	if(!*keys) return -1;
	*values = calloc(1, bufLen);
	if(!*values) return -1;

	strcat(*keys, "radio\n");
	strcat(*values, remote_bool_str[connection.radio]);
	strcat(*values, "\n");
	strcat(*keys, "registration\n");
	strcat(*values, remote_bool_str[connection.registration]);
	strcat(*values, "\n");
	strcat(*keys, "version\n");
	strcat(*values, connection.fwVersion);
	strcat(*values, "\n");
	strcat(*keys, "spectrum\n");
	strcat(*values, connection.type);
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
		if (!reset_ind || abs(time(NULL) - reset_ind) > MIN_RESET_WAIT_TIME) {
			printf("External Dect found\n");
			syslog(LOG_INFO, "External Dect found");
			memset(connection.rfpi, 0, 5);
			memset(connection.fwVersion, 0, sizeof(connection.fwVersion));
			memset(connection.type, 0, sizeof(connection.type));
			connection.hasInitialized = 0;
			handsets.termCntExpt = -1;
			reset_ind = time(NULL);
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

			printf("Internal Dect found\n");
			syslog(LOG_INFO, "Internal Dect found");
			if(resp->NvsSize != DECT_NVS_SIZE) {
				exit_failure("Invalid NVS size from driver, something is wrong!\n");
			}
			else if(resp->MaxMailSize == 0) {
				/* Internal Dect has already been initialized previously
				 * and we MUST only do the init once, so skip it. */
				ApiFpGetFwVersionReqType m1 = {
					.Primitive = API_FP_GET_FW_VERSION_REQ,
				};
				printf("WRITE: API_FP_GET_FW_VERSION_REQ\n");
				mailProto.send(dect_bus, (uint8_t *) &m1,
					sizeof(ApiFpGetFwVersionReqType));
			}
			else {
				// Initialization is needed, send NVS data to kernel driver
				ApiLinuxInitReqType *req = (ApiLinuxInitReqType*)
					malloc(sizeof(ApiLinuxInitReqType) + DECT_NVS_SIZE);
				memset(req, 0, sizeof(ApiLinuxInitReqType) + DECT_NVS_SIZE);
				req->Primitive = API_LINUX_INIT_REQ;
	
				if(nvs_file_read((uint32_t*) &req->LengthOfData, 
						req->Data) == 0 && nvs_rfpi_patch(req->Data) == 0 &&
						nvs_freq_patch(req->Data) == 0 &&
						nvs_emc_patch(req->Data) == 0) {
					mailProto.send(dect_bus, (uint8_t*) req,
						sizeof(ApiLinuxInitReqType) + req->LengthOfData - 1);
				}
				else {
					printf("Error initializing Dect with NVS\n");
					syslog(LOG_WARNING, "Error initializing Dect with NVS");
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

	case API_LINUX_INIT_CFM:													// Internal Dect has initialized for the first time
		{
			ApiFpGetFwVersionReqType m1 = {
				.Primitive = API_FP_GET_FW_VERSION_REQ,
			};
			printf("WRITE: API_FP_GET_FW_VERSION_REQ\n");
			mailProto.send(dect_bus, (uint8_t *) &m1,
				sizeof(ApiFpGetFwVersionReqType));
		}
		break;

	case API_FP_GET_FW_VERSION_CFM:
		{
			ApiFpMmGetIdReqType req = {
				.Primitive = API_FP_MM_GET_ID_REQ,
			};
			if(connection.hasInitialized) break;								// Do nothing if third party rawmail app use same command
			fw_version_cfm(m);
			printf("WRITE: API_FP_MM_GET_ID_REQ\n");
			mailProto.send(dect_bus, (uint8_t *) &req, sizeof(req));
		}
		break;

	case API_FP_MM_GET_ID_CFM: {
		ApiFpCcFeaturesReqType req = {
			.Primitive = API_FP_CC_FEATURES_REQ,
			.ApiFpCcFeature = API_FP_CC_EXTENDED_TERMINAL_ID_SUPPORT
		};

		ApiFpMmGetIdCfmType *prodResp =
			(ApiFpMmGetIdCfmType*) &m->mail_header;

		if(connection.hasInitialized) break;									// Do nothing if third party rawmail app use same command

		// We have read the device RFPI. Now verify it's correct
		if(prodResp->Status == RSS_SUCCESS) {
			memcpy(connection.rfpi, prodResp->Id, 5);
			printf("Read RFPI from NVS: %2.2x %2.2x %2.2x %2.2x %2.2x\n",
				prodResp->Id[0], prodResp->Id[1], prodResp->Id[2],
				prodResp->Id[3], prodResp->Id[4]);

			if(is_radio_prerequisites_ok()) {
				printf("WRITE: API_FP_CC_FEATURES_REQ\n");
				mailProto.send(dect_bus, (uint8_t *) &req, sizeof(req));
			}

			/* The NVS RFPI was found to be invalid, we try to
			 * repair it now by activating factory setup. */
			else if(reprogram_clock_freq()) {
				printf("Failed reprograming clock frequency in FP!\n");
				syslog(LOG_WARNING, "Failed reprograming clock frequency in FP!");
			}
		}
		else {
			exit_failure("Failed ID query");
		}
	}
	break;

	case API_PROD_TEST_CFM: {													// Factory production commands; only used once, when external dect flash is empty.
		ApiProdTestCfmType *prodResp = (ApiProdTestCfmType*) &m->mail_header;
		int factorySetup, needReset;

		factorySetup = 1;
		needReset = 0;

		switch(prodResp->Opcode) {												// Urg, production commands use neasted opcodes!
			case PT_CMD_SET_CLOCK_FREQUENCY: {									// We have reprogrammed external dect radio calibration clock
				PtSetFreqCfmType *ptResp =
					(PtSetFreqCfmType*) prodResp->Parameters;

				if(prodResp->ParameterLength == sizeof(PtSetFreqCfmType) &&
						ptResp->Status == RSS_SUCCESS) {						
					if(reprogram_rfpi()) {
						printf("Failed reprograming RFPI in FP!\n");
						syslog(LOG_WARNING, "Failed reprograming RFPI in FP!");
						needReset = 1;
					}
				}
				else {
					printf("Failed reprograming clock frequency in FP!\n");
					syslog(LOG_WARNING, "Failed reprograming clock frequency in FP!");
				}
			}
			break;

			case PT_CMD_SET_ID: {												// We have reprogrammed Natalie RFPI
				PtSetIdCfmType *ptResp =
					(PtSetIdCfmType*) prodResp->Parameters;

				if(prodResp->ParameterLength == sizeof(PtSetIdCfmType) &&
						ptResp->Status == RSS_SUCCESS) {
					needReset = 1;
					factorySetup = 0;
				}
				else {
					printf("Failed reprograming RFPI in FP!\n");
					syslog(LOG_WARNING, "Failed reprograming RFPI in FP!");
				}
			}
			break;

			default:
				needReset = 1;
				break;
		}

		/* When factory production setup has completed
		 * we reset the Dect chip and start all over again. */
		if(needReset) {
			dect_warm_reset();
		}
		else if(!factorySetup) {
			exit_failure("Failed factory production");
		}
	}
	break;

	case API_FP_CC_FEATURES_CFM:
		if(connection.hasInitialized) break;									// Do nothing if third party rawmail app use same command

		if(ubus_enable_receive()) {
			break;
		}
		else if(hwIsInternal) {
			/* Start protocol (the radio) if
			 * it's configured by user. */
			ubus_send_string("dectmngr", ubusStrActive);
			if(uci_call_query("radio")) list_handsets();
		}
		else {
			/* Init external Dect PCM bus */
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
							 .PcmDoutIsOpenDrain = 0x0, /* Must be 1 if mult. devices on bus */
							 .PcmIsOpenDrain = 0x0,  /* 0 == Normal mode */
			};
			mailProto.send(dect_bus, (uint8_t *)&pcm_req, sizeof(ApiFpInitPcmReqType));
		}
		break;

	case API_FP_INIT_PCM_CFM:
		{
			ApiFpInitPcmCfmType * resp = (ApiFpInitPcmCfmType *) &m->mail_header;
			print_status(resp->Status);

			/* Start protocol (the radio) if
			 * it's configured by user. */
			ubus_send_string("dectmngr", ubusStrActive);
			if(uci_call_query("radio")) list_handsets();
		}
		break;

	case API_FP_MM_SET_ACCESS_CODE_CFM:											// PIN code for registering
		{
			ApiFpMmSetAccessCodeCfmType *resp =
				(ApiFpMmSetAccessCodeCfmType*)  &m->mail_header;

			if(resp->Status == RSS_SUCCESS) {
				if(connection.accessCodeStatus == PENDING_ACTIVE) {
					connection.accessCodeStatus = ACTIVE;
				}

				if(connection.registration == PENDING_ACTIVE) {
					/* We need to work together here with third
					 * party applications which as well may
					 * activate registration. */
					connection_set_registration(1);
				}
			}
			else {
				connection_set_registration(0);
			}
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
					syslog(LOG_INFO, "Registration is active");
					ubus_send_string("registration", ubusStrActive);			// Send ubus event
					ubus_call_string("led.dect", "set", "state", "notice", NULL);// Light up box LED
				}
				else if(connection.registration == PENDING_INACTIVE) {
					connection.registration = INACTIVE;
					printf("Registration is inactive\n");
					syslog(LOG_INFO, "Registration is inactive");
					ubus_send_string("registration", ubusStrInActive);
					ubus_call_string("led.dect", "set", "state", 
						(connection.radio == ACTIVE) ? "ok" : "off", NULL);
if(handsets.termCntEvntAdd || handsets.termCntEvntDel) list_handsets();
				}
			}
		}
		break;

	case API_FP_MM_REGISTRATION_COMPLETE_IND:
		if(connection.registration == ACTIVE) connection_set_registration(0);
		break;

	case API_FP_ULE_PVC_CONFIG_IND:
		printf("API_FP_ULE_PVC_CONFIG_IND ----------------------------------------\n");
		break;

	case API_FP_ULE_PVC_PENDING_IND:
		printf("API_FP_ULE_PVC_PENDING_IND ----------------------------------------\n");
		break;

	case API_FP_ULE_PVC_IWU_DATA_IND:
		printf("API_FP_ULE_PVC_IWU_DATA_IND ---------------------------------------\n");
		break;
	}

}



//-------------------------------------------------------------
// Timer handler, for turning of registration
// after a delay if no handset has registered
// and turning off the radio after a delay as
// well due to this application will exit(),
// but first we need some time to send a reply
// for the Ubus request that asked us to turn
// the radio off.
static void timer_handler(void *unused1 __attribute__((unused)), void *unused2 __attribute__((unused))) {
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
	else if(connection.radio == PENDING_INACTIVE) {
		/* By now we have had time to send a ubus reply
		 * for radio off request. Do application exit.
		 * After radio has been disabled we need
		 * to reset the Dect firmware stack. It's
		 * the only way to enable reactivation of
		 * the radio, should the user want to. */
		dect_warm_reset();
	}
	else if(!connection.hasInitialized) {
		printf("Warning, timeout initializing dect\n");
		syslog(LOG_WARNING, "Warning, timeout initializing");
	}
}



//-------------------------------------------------------------
void connection_init(void * bus) {
	struct itimerspec newTimer;

	dect_bus = bus;
	mailProto.add_handler(bus, connection_init_handler);

	timer_fd = timerfd_create(CLOCK_MONOTONIC, O_NONBLOCK);
	if(timer_fd == -1) {
		exit_failure("Error creating handset timer");
	}

	timer_stream = stream_new(timer_fd);
	stream_add_handler(timer_stream, 0, timer_handler);
	event_base_add_stream(timer_stream);

	/* Start a timer for when we expect to have
	 * initialized Natalie dect. If it times out
	 * something is wrong with Natalie firmware. */
	memset(&newTimer, 0, sizeof(newTimer));
	newTimer.it_value.tv_sec = MAX_RESET_INIT_TIME;
	if(timerfd_settime(timer_fd, 0, &newTimer, NULL) == -1) {
		perror("Error setting timer");
	}

	/* At startup of dectmngr the dect chip is
	 * reseted and thus we know initial state. */
	connection.radio = INACTIVE;
	connection.registration = INACTIVE;
	connection.accessCodeStatus = INACTIVE;
	connection.uciRadioConf = RADIO_AUTO;
	connection.hasInitialized = 0;
}



//-------------------------------------------------------------
/* Start or stop protocol (the DECT radio) */
int connection_set_radio(int onoff) {
	struct itimerspec newTimer;

	memset(&newTimer, 0, sizeof(newTimer));
	newTimer.it_value.tv_sec = 1;

	if(onoff) {
		if(!is_radio_prerequisites_ok()) {
			ubus_send_string("radio", ubusStrInActive);
			return -1;
		}

		/* Don't enable radio again if it's already active
		 * since it would inactivate registration in the
		 * timer handler. */
		if(connection.radio == ACTIVE) return 0;

		ApiFpMmStartProtocolReqType r =  { .Primitive = API_FP_MM_START_PROTOCOL_REQ, };
		mailProto.send(dect_bus, (uint8_t *)&r, sizeof(ApiFpMmStartProtocolReqType));
		usleep(200000);
		connection.radio = ACTIVE;												// No confirmation is replied
		printf("Radio is active\n");
		ubus_send_string("radio", ubusStrActive);
		ubus_call_string("led.dect", "set", "state", "ok", NULL);
	}
	else if(connection.radio == ACTIVE) {
		ApiFpMmStopProtocolReqType reqOff = {
			.Primitive = API_FP_MM_STOP_PROTOCOL_REQ, };

		mailProto.send(dect_bus, (uint8_t *) &reqOff,
			sizeof(ApiFpMmStopProtocolReqType));
		connection.radio = INACTIVE;											// No confirmation is replied

		/* Schedule a delayed radio off due to our caller
		 * needs some time to send a ubus reply. */
		connection.radio = PENDING_INACTIVE;
		newTimer.it_value.tv_sec = 0;
		newTimer.it_value.tv_nsec = 1e7;
	}

	if(timerfd_settime(timer_fd, 0, &newTimer, NULL) == -1) {
		perror("Error setting timer");
	}

	return 0;
}



//-------------------------------------------------------------
// Enable registration of phones and arm a
// timer for possible timeout.
int connection_set_registration(int onoff) {
	ApiFpMmSetRegistrationModeReqType setRegistr;
	ApiFpMmSetAccessCodeReqType setAccessCode;
	struct itimerspec newTimer;
	int res;

	setRegistr.Primitive = API_FP_MM_SET_REGISTRATION_MODE_REQ;
	setRegistr.DeleteLastHandset = true;
	setAccessCode.Primitive = API_FP_MM_SET_ACCESS_CODE_REQ;
	setAccessCode.Ac[0] = 0xffu;
	setAccessCode.Ac[1] = 0xffu;
	setAccessCode.Ac[2] = 0;													// PIN code for handset pairing (BCD encoded?).
	setAccessCode.Ac[3] = 0;													//  TODO: read it from uci.

	memset(&newTimer, 0, sizeof(newTimer));

	// Activate or inactivate registration?
	if(onoff) {
		if(!is_radio_prerequisites_ok()) {
			ubus_send_string("radio", ubusStrInActive);
			return -1;
		}

		connection.registration = PENDING_ACTIVE;

		/* Was the radio in progress of becomming
		 * turned off? Then abort that operation. */
		if(connection.radio == PENDING_INACTIVE) connection.radio = ACTIVE;

		// Do we need to set the PIN code first?
		if(connection.accessCodeStatus != ACTIVE) {
			printf("WRITE: API_FP_MM_SET_ACCESS_CODE_REQ\n");
			connection.accessCodeStatus = PENDING_ACTIVE;
			mailProto.send(dect_bus, (uint8_t *) &setAccessCode,
				sizeof(setAccessCode));
		}

		/* The PIN code has been set. Is the radio on? Then
		 * start registration and a timout timer. */
		else if(connection.radio == ACTIVE) {
			printf("WRITE: API_FP_MM_SET_REGISTRATION_MODE_REQ\n");
			newTimer.it_value.tv_sec = 180;
			setRegistr.RegistrationEnabled = 2;
			mailProto.send(dect_bus, (uint8_t *) &setRegistr,
				sizeof(ApiFpMmSetRegistrationModeReqType));
		}

		else {
			/* The radio was off, we need to start it
			 * before we can start registration. */
			res = connection_set_radio(1);
			if(res == -1) {
				return -1;
			}
			else if(res == 0) {
				newTimer.it_value.tv_sec = 1;
			}
		}
	}
	else {
		// Turn of registration
		connection.accessCodeStatus = INACTIVE;
		connection.registration = PENDING_INACTIVE;
		setRegistr.RegistrationEnabled = 0;
		mailProto.send(dect_bus, (uint8_t *) &setRegistr,
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
	DECTSHIMDRV_CHANNELCOUNT_PARAM chanCnt;
	DECTSHIMDRV_INIT_PARAM parm;
	int64_t minPktDelay;
	int shimFd, r;

	shimFd = open("/dev/dectshim", O_RDWR);
	if (shimFd == -1) exit_failure("%s: open error %d\n", __FUNCTION__, errno);

	memset(&parm, 0, sizeof(parm));
	r = ioctl(shimFd, DECTSHIMIOCTL_INIT_CMD, &parm);
	if (r != 0) {
		exit_failure("%s: ioctl error shm init%d\n", __FUNCTION__, errno);
	}

	memset(&chanCnt, 0, sizeof(chanCnt));
	r = ioctl(shimFd, DECTSHIMIOCTL_GET_CHANNELS_CMD, &chanCnt);
	if (r != 0) {
		exit_failure("%s: ioctl error shm get channels%d\n",
			__FUNCTION__, errno);
	}
	hwIsInternal = chanCnt.channel_count > 0;

	close(shimFd);

	minPktDelay = 5000LL;														// Minimum delay in usec between transmitted packets
	mailProto.conf(dect_bus, MIN_PKT_DELAY, &minPktDelay);

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
	if(connection.registration == PENDING_ACTIVE) return 0;
	if(connection.registration == PENDING_INACTIVE) return 0;

	if(connection.uciRadioConf == RADIO_AUTO) {
		if(hasProxyClient()) {													// Are there any third party client connected?
			return connection_set_radio(1);
		}
		else if(handsets.termCntExpt >= 0 &&
				handsets.termCntExpt == handsets.termCount) {
			return connection_set_radio(handsets.termCount > 0);
		}
	}
	else if(connection.uciRadioConf == RADIO_ALWAYS_ON) {
		return connection_set_radio(1);
	}
	else if(connection.uciRadioConf == RADIO_ALWAYS_OFF) {
		return connection_set_radio(0);
	}

	return 0;
}


