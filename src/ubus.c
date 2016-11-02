#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <libubus.h>
#include <libubox/blobmsg_json.h>
#include "ubus.h"
#include "util.h"
#include "error.h"
#include "stream.h"
#include "event_base.h"
#include "connection_init.h"
#include "external_call.h"
#include "dect.h"


//-------------------------------------------------------------
enum {
	STATE_RADIO,
	STATE_REGISTRATION,
	STATE_UNUSED,
};

enum {
	BTN_ACTN,
	BTN_TYPE,
};

enum {
	HANDSET_LIST,
	HANDSET_DELETE,
	HANDSET_PAGEALL,															// Page (ping) all handsets
};

enum {
	CALL_TERM,																	// Terminal ID
	CALL_ADD,																	// Add call using PCMx
	CALL_REL,																	// Release call using PCMx
	CALL_CID,																	// Caller ID
};

enum {
	SETTING_RADIO,
};

enum {
	AST_TERM,
	AST_PCM,
	AST_ERR,
};


struct querier_t {
	int inUse;																	// Free or busy?
	struct timespec timeStamp;													// Timestamp of request
	struct ubus_request_data req;
	struct ubus_context *ubus_ctx;
};


static int ubus_request_state(struct ubus_context *ubus_ctx, struct ubus_object *obj,
		struct ubus_request_data *req, const char *methodName, struct blob_attr *msg);
static int ubus_request_handset(struct ubus_context *ubus_ctx, struct ubus_object *obj,
		struct ubus_request_data *req, const char *methodName, struct blob_attr *msg);
static int ubus_request_status(struct ubus_context *ubus_ctx, struct ubus_object *obj,
		struct ubus_request_data *req, const char *methodName, struct blob_attr *msg);
static int ubus_request_call(struct ubus_context *ubus_ctx, struct ubus_object *obj,
		struct ubus_request_data *req, const char *methodName, struct blob_attr *msg);



//-------------------------------------------------------------
const char ubusSenderPath[] = "dect";											// The Ubus type we transmitt
static const char ubusPathAsterisk[] = "asterisk.dect.api";						// The Ubus type for Asterisk
const char ubusStrActive[] = "active";											// The "bool" we transmitt for service is on/enabled/operational
const char ubusStrInActive[] = "inactive";
const char strOn[] = "on";
const char strOff[] = "off";
const char strAuto[] = "auto";
const char strPressed[] = "pressed";											// Button pressed string
const char strReleased[] = "released";
const char strShort[] = "short";
const char strLong[] = "long";
const char strErrno[] = "errno";
const char strButton[] = "button.DECT";											// Ubus button event names


static const struct blobmsg_policy ubusStateKeys[] = {							// ubus RPC "state" arguments (keys and values)
	[STATE_RADIO] = { .name = "radio", .type = BLOBMSG_TYPE_STRING },
	[STATE_REGISTRATION] = { .name = "registration", .type = BLOBMSG_TYPE_STRING },

	/*[STATE_UNUSED] = { .name = "unused", .type = BLOBMSG_TYPE_INT32 },*/
};


static const struct blobmsg_policy ubusHandsetKeys[] = {						// ubus RPC "handset" arguments (keys and values)
	[HANDSET_LIST] = { .name = "list", .type = BLOBMSG_TYPE_STRING },
	[HANDSET_DELETE] = { .name = "delete", .type = BLOBMSG_TYPE_INT32 },
	[HANDSET_PAGEALL] = { .name = "pageall", .type = BLOBMSG_TYPE_STRING },
};


static const struct blobmsg_policy ubusCallKeys[] = {							// ubus RPC "call" arguments (keys and values)
	[CALL_TERM] = { .name = "terminal", .type = BLOBMSG_TYPE_INT32 },
	[CALL_ADD] = { .name = "add", .type = BLOBMSG_TYPE_INT32 },
	[CALL_REL] = { .name = "release", .type = BLOBMSG_TYPE_INT32 },
	[CALL_CID] = { .name = "cid", .type = BLOBMSG_TYPE_STRING },
};

static const struct ubus_method ubusMethods[] = {								// ubus RPC methods
	UBUS_METHOD("state", ubus_request_state, ubusStateKeys),
	UBUS_METHOD("handset", ubus_request_handset, ubusHandsetKeys),
	UBUS_METHOD_NOARG("status", ubus_request_status),
	UBUS_METHOD("call", ubus_request_call, ubusCallKeys),
};


static const struct blobmsg_policy buttonKeys[] = {
	[BTN_ACTN] = { .name = "action", .type = BLOBMSG_TYPE_STRING },				// Button press or release
	[BTN_TYPE] = { .name = "type", .type = BLOBMSG_TYPE_STRING },				// Long or short press
};


static const struct blobmsg_policy uciKeys[] = {
	[SETTING_RADIO] = { .name = "value", .type = BLOBMSG_TYPE_STRING },
};

static const struct blobmsg_policy asteriskKeys[] = {
	[AST_TERM] = { .name = "terminal", .type = BLOBMSG_TYPE_INT32 },
	[AST_PCM] = { .name = "pcm", .type = BLOBMSG_TYPE_INT32 },
	[AST_ERR] = { .name = strErrno, .type = BLOBMSG_TYPE_INT32 },
};


static struct ubus_object_type rpcType;
static struct ubus_object rpcObj;
static struct ubus_context *ubusContext;
static void *ubus_stream;
static int isReceiveing;														// True when ubus receiving is enabled
static struct ubus_event_handler stateListener;									// Event handler for radio and registration 
static struct ubus_event_handler buttonListener;								// Event handler box dect button
static struct querier_t querier;												// Handle to deferred RPC request
static uint32_t uciId;
static uint32_t asteriskId;



//-------------------------------------------------------------
// We transmitt many strings as an ubus event to the specified path
int ubus_send_strings(const char *path, const char *msgKey[], const char *msgVal[], int len)
{
	struct blob_buf blob;
	int i, res = 0;

	memset(&blob, 0, sizeof(blob));
	if(blob_buf_init(&blob, 0)) return -1;
	for(i = 0; i < len; i++) blobmsg_add_string(&blob, msgKey[i], msgVal[i]);

	if(ubus_send_event(ubusContext, path, blob.head) != UBUS_STATUS_OK) {
		printf("Error sending ubus message %s %s\n", msgKey[0], msgVal[0]);
			res = -1;
	}

	blob_buf_free(&blob);

	return res;
}



//-------------------------------------------------------------
// We transmitt one string to as an ubus event to the specified path
int ubus_send_string_to(const char *path, const char *msgKey, const char *msgVal)
{
	return ubus_send_strings(path, &msgKey, &msgVal, 1);
}



//-------------------------------------------------------------
// We transmitt one string as a public (to all) ubus event
int ubus_send_string(const char *msgKey, const char *msgVal) {
	return ubus_send_string_to(ubusSenderPath, msgKey, msgVal);	
}



//-------------------------------------------------------------
// Callback for: a ubus call (invocation) has replied with some data
static void call_answer(struct ubus_request *req, int type, struct blob_attr *msg)
{
	unsigned int maxKeys = ARRAY_SIZE(uciKeys);
	if(ARRAY_SIZE(asteriskKeys) > maxKeys) maxKeys = ARRAY_SIZE(asteriskKeys);
	struct blob_attr *keys[maxKeys];
	int termId, pcmId, err;
	const char *strVal;

	if(type != UBUS_MSG_DATA) return;
	if(req->status_code != UBUS_STATUS_OK) printf("Got error answer\n");

	// Got answer from an UCI query?
	if(req->peer == uciId) {

		// Tokenize message key/value paris into an array
		if(blobmsg_parse(uciKeys, ARRAY_SIZE(uciKeys), keys, 
				blob_data(msg), blob_len(msg))) {
			return;
		}

		// Handle UCI setting "radio" == on/off/auto
		if(keys[SETTING_RADIO]) {
			strVal = blobmsg_get_string(keys[SETTING_RADIO]);

			if(strncmp(strVal, strOn, sizeof(strOn)) == 0) {
				// Radio should always be on
				printf("Uci setting radio permanently active\n");
				connection.uciRadioConf = RADIO_ALWAYS_ON;
				connection_set_radio(1);
			}
			else if(strncmp(strVal, strOff, sizeof(strOff)) == 0) {
				// Radio should always be off
				printf("Uci setting radio permanently inactive\n");
				connection.uciRadioConf = RADIO_ALWAYS_OFF;
				connection_set_radio(0);
			}
			else {
				/* Default to radio should be on if we have
				 * registered handsets, otherwise off. */
				printf("Uci setting radio active when handsets registered\n");
				connection.uciRadioConf = RADIO_AUTO;
			}

			list_handsets();
		}
	}

	// Got answer from an Asterisk query?
	else if(req->peer == asteriskId) {
		termId = pcmId = err = -1;
printf("Got incomming ubus answer\n");

		// Tokenize message key/value paris into an array
		if(blobmsg_parse(asteriskKeys, ARRAY_SIZE(asteriskKeys), keys, 
				blob_data(msg), blob_len(msg))) {
			return;
		}

		if(keys[AST_TERM]) {
			termId = blobmsg_get_u32(keys[AST_TERM]);
printf("Call answer terminal %d\n", termId);
		}

		if(keys[AST_PCM]) {
			pcmId = blobmsg_get_u32(keys[AST_PCM]);
printf("Call answer pcm %d\n", pcmId);
		}

		if(keys[AST_ERR]) {
			err = blobmsg_get_u32(keys[AST_ERR]);
printf("Call answer err %d\n", err);
		}

		asterisk_cfm(pcmId, err);
	}
}



//-------------------------------------------------------------
// Callback for: a ubus call (invocation) has finished
static void call_complete(struct ubus_request *req, int ret)
{
	ubus_call_complete_callback cb = (ubus_call_complete_callback) req->priv;

	if(uciId == req->peer && req->status_code != UBUS_STATUS_OK) {
		/* Got answer from a _failed_ UCI query and thus, we
		 * can't query UCI for users radio settings. Do
		 * something sane as default. */
		list_handsets();
	}

	if(cb) cb(ret);
	free(req);
}



//-------------------------------------------------------------
// Common code for sending a call invocation away
static int ubus_call_blob(uint32_t id, const char* method, struct blob_buf *blob, 
	ubus_call_complete_callback cb)
{
	struct ubus_request *req;
	int res;

	res = 0;
	req = calloc(1, sizeof(struct ubus_request));
	if(!req) return -1;

	/* Call remote method, without
	 * waiting for completion. */
	res = ubus_invoke_async(ubusContext, id, method, blob->head, req);
	if(res != UBUS_STATUS_OK) {
		printf("Error invoking\n");
		res = -1;
		goto out;
	}

	/* Mark the call as non blocking. When
	 * it completes we get "called back". */
	req->data_cb = call_answer;
	req->complete_cb = call_complete;
	req->priv = cb;
	ubus_complete_request_async(ubusContext, req);

out:
	blob_buf_free(blob);

	return res;
}



//-------------------------------------------------------------
// We invoke someone with ubus RPC and a string argument
int ubus_call_string(const char *path, const char* method, const char *key, 
	const char *val, ubus_call_complete_callback cb)
{
	struct blob_buf blob;
	uint32_t id;
	int res;

	res = 0;
	memset(&blob, 0, sizeof(blob));
	if(blob_buf_init(&blob, 0)) return -1;
	blobmsg_add_string(&blob, key, val);

	// Find id number for ubus "path"
	res = ubus_lookup_id(ubusContext, path, &id);
	if(res != UBUS_STATUS_OK) {
		printf("Error searching for usbus path %s\n", path);
		return -1;
	}

	return ubus_call_blob(id, method, &blob, cb);
}



//-------------------------------------------------------------
// Query UCI for settings. The reply arrives in call_answer().
int uci_call_query(const char *option)
{
	struct blob_buf blob;

	memset(&blob, 0, sizeof(blob));
	if(blob_buf_init(&blob, 0)) return -1;
	blobmsg_add_string(&blob, "config", ubusSenderPath);
	blobmsg_add_string(&blob, "section", ubusSenderPath);
	blobmsg_add_string(&blob, "option", option);

	return ubus_call_blob(uciId, "get", &blob, 0);
}



//-------------------------------------------------------------
// Send a ubus request to Asterisk. The reply arrives in call_answer().
int asterisk_call(int terminal, int add, int release, const char *cid)
{
	struct blob_buf blob;
	int res;

	// Find id number for ubus "path"
	res = ubus_lookup_id(ubusContext, ubusPathAsterisk, &asteriskId);
	if(res != UBUS_STATUS_OK) {
		printf("Error searching for usbus path %s\n", ubusPathAsterisk);
		return -1;
	}

	// Create a binary ubus message
	memset(&blob, 0, sizeof(blob));
	if(blob_buf_init(&blob, 0)) return -1;
	if(terminal >= 0) blobmsg_add_u32(&blob, ubusCallKeys[CALL_TERM].name, terminal);
	if(add >= 0) blobmsg_add_u32(&blob, ubusCallKeys[CALL_ADD].name, add);
	if(release >= 0) blobmsg_add_u32(&blob, ubusCallKeys[CALL_REL].name, release);
	if(cid) blobmsg_add_string(&blob, ubusCallKeys[CALL_CID].name, cid);

printf("Sending ubus request %d %d %d %s\n", terminal, add, release, cid);
	return ubus_call_blob(asteriskId, "call", &blob, 0);
}


//-------------------------------------------------------------
// Send a reply that a request was successful
static int ubus_reply_success(struct ubus_context *ubus_ctx,
		struct ubus_request_data *req, const char *methodName)
{
	struct blob_buf isOkMsg;

	memset(&isOkMsg, 0, sizeof(isOkMsg));
	if(blobmsg_buf_init(&isOkMsg)) return -1;

	blobmsg_add_u32(&isOkMsg, "errno", 0);
	blobmsg_add_string(&isOkMsg, "errstr", strerror(0));
	blobmsg_add_string(&isOkMsg, "method", methodName);

	ubus_send_reply(ubus_ctx, req, isOkMsg.head);
	blob_buf_free(&isOkMsg);

	return 0;
}




//-------------------------------------------------------------
// Send an error indicator as reply to a call invocation
static int ubus_reply_err(struct ubus_context *ubus_ctx,
		struct ubus_request_data *req, const char *methodName, uint32_t err)
{
	struct blob_buf msg;

	memset(&msg, 0, sizeof(msg));
	if(blobmsg_buf_init(&msg)) return -1;

	blobmsg_add_u32(&msg, "errno", err);
	blobmsg_add_string(&msg, "errstr", strerror(err));
	blobmsg_add_string(&msg, "method", methodName);

	ubus_send_reply(ubus_ctx, req, msg.head);
	blob_buf_free(&msg);

	return 0;
}


//-------------------------------------------------------------
// Send a busy reply that "we can't handle caller
// request at the moment. Please retry later."
static int ubus_reply_busy(struct ubus_context *ubus_ctx,
		struct ubus_request_data *req, const char *methodName)
{
	return ubus_reply_err(ubus_ctx, req, methodName, EBUSY);
}



//-------------------------------------------------------------
// Send a list of registered phones as reply
int ubus_reply_handset_list(int retErrno, const struct handsets_t const *handsets)
{
	struct blob_buf blob;
	void *list1, *tbl, *list2;
	int i, j;

	if(!querier.inUse) return -1;

	memset(&blob, 0, sizeof(blob));
	if(blobmsg_buf_init(&blob)) return -1;

	list1 = blobmsg_open_array(&blob, "handsets");
	for(i = 0; i < MAX_NR_HANDSETS; i++) {
		if(!handsets->terminal[i].id) continue;
		if(!handsets->terminal[i].codecs) continue;

		tbl = blobmsg_open_table(&blob, NULL);
		blobmsg_add_u32(&blob, "id", handsets->terminal[i].id);

		list2 = blobmsg_open_array(&blob, "ipui");
		blobmsg_add_u16(&blob, NULL, handsets->terminal[i].ipui[0]);
		blobmsg_add_u16(&blob, NULL, handsets->terminal[i].ipui[1]);
		blobmsg_add_u16(&blob, NULL, handsets->terminal[i].ipui[2]);
		blobmsg_add_u16(&blob, NULL, handsets->terminal[i].ipui[3]);
		blobmsg_add_u16(&blob, NULL, handsets->terminal[i].ipui[4]);
		blobmsg_close_table(&blob, list2);

		list2 = blobmsg_open_array(&blob, "codecs");
		for(j = 0; j < handsets->terminal[i].codecs->NoOfCodecs; j++) {
			switch(handsets->terminal[i].codecs->Codec[j].Codec) {
				case API_CT_G726:
					blobmsg_add_string(&blob,  NULL, "G.726");
					break;
				case API_CT_G722:
					blobmsg_add_string(&blob,  NULL, "G.722");
					break;
				case API_CT_G711A:
					blobmsg_add_string(&blob,  NULL, "G.711A");
					break;
				case API_CT_G711U:
					blobmsg_add_string(&blob,  NULL, "G.711U");
					break;
				case API_CT_G7291:
					blobmsg_add_string(&blob,  NULL, "G.791");
					break;
			}
		}
		blobmsg_close_table(&blob, list2);

		blobmsg_close_table(&blob, tbl);
	}
	blobmsg_close_table(&blob, list1);

	// Add returncode
	blobmsg_add_u32(&blob, "errno", retErrno);
	blobmsg_add_string(&blob, "errstr", strerror(retErrno));

	ubus_send_reply(querier.ubus_ctx, &querier.req, blob.head);
	blob_buf_free(&blob);

	/* Convert return code from callee
	 * to a UBUS status equivalent. */
	retErrno = (retErrno ? UBUS_STATUS_UNKNOWN_ERROR : UBUS_STATUS_OK);

	ubus_complete_deferred_request(querier.ubus_ctx, &querier.req, retErrno);
	querier.inUse = 0;

	return 0;
}



//-------------------------------------------------------------
// Are we ready for another deferred request? We can only handle
// one at a time. Check for a possible previous timeout and discard
// the previous if so to free resources.
static void perhaps_reply_deferred_timeout(void) {
	struct timespec now;

	if(!querier.inUse) return;
	if(clock_gettime(CLOCK_MONOTONIC, &now)) return;

	if(now.tv_sec - querier.timeStamp.tv_sec > 40) {							// Long timeout due to handset deletes can be slow
		ubus_complete_deferred_request(querier.ubus_ctx, &querier.req, 
			UBUS_STATUS_TIMEOUT);
		querier.inUse = 0;
	}
}



//-------------------------------------------------------------
// Event handler for
// ubus send button.DECT '{ "action": "pressed" }'
static void ubus_event_button(struct ubus_context *ctx __attribute__((unused)), struct ubus_event_handler *ev __attribute__((unused)), const char *type __attribute__((unused)), struct blob_attr *blob)
{
	struct blob_attr *keys[ARRAY_SIZE(buttonKeys)];
	const char *strVal;

	// Tokenize message key/value paris into an array
	if(blobmsg_parse(buttonKeys, ARRAY_SIZE(buttonKeys),
			keys, blob_data(blob), blob_len(blob))) {
		return;
	}

	/* Do nothing on button presses, we don't want to
	 * trigger a radio activity just before we might
	 * turn radio off (at button release). */
	if(keys[BTN_ACTN]) {
		strVal = blobmsg_get_string(keys[BTN_ACTN]);
		if(strncmp(strVal, strReleased, sizeof(strReleased))) return;
	}

	// Long or short button press?
	if(keys[BTN_TYPE]) {
		strVal = blobmsg_get_string(keys[BTN_TYPE]);

		// Long button released?
		if(strncmp(strVal, strLong, sizeof(strLong)) == 0) {
			if(connection.uciRadioConf == RADIO_ALWAYS_ON) {
				printf("Button inactivates radio permanently\n");
				connection.uciRadioConf = RADIO_ALWAYS_OFF;
				connection_set_radio(0);
			}
			else {
				printf("Button activates radio permanently\n");
				connection.uciRadioConf = RADIO_ALWAYS_ON;
				connection_set_registration(1);
			}
		}

		// Short button released?
		else if(strncmp(strVal, strShort, sizeof(strShort)) == 0) {
			if(connection.uciRadioConf == RADIO_ALWAYS_OFF) {
				printf("Ignoring button, radio is inactive\n");
			}
			else {
				printf("Button activates registration\n");
				connection_set_registration(1);
			}
		}
	}
}



//-------------------------------------------------------------
// Event handler for
// ubus send dect '{ "key": "value" }'
static void ubus_event_state(struct ubus_context *ctx __attribute__((unused)), struct ubus_event_handler *ev __attribute__((unused)), const char *type __attribute__((unused)), struct blob_attr *blob)
{
	struct blob_attr *keys[ARRAY_SIZE(ubusStateKeys)];
	const char *strVal;

	// Tokenize message key/value paris into an array
	if(blobmsg_parse(ubusStateKeys, ARRAY_SIZE(ubusStateKeys),
			keys, blob_data(blob), blob_len(blob))) {
		return;
	}

	/* Handle event:
	 * ubus send dect '{ "radio": "off" }' */
	if(keys[STATE_RADIO]) {
		strVal = blobmsg_get_string(keys[STATE_RADIO]);
		if(strncmp(strVal, strOn, sizeof(strOn)) == 0) {
			connection_set_radio(1);
		}
		else if(strncmp(strVal, strOff, sizeof(strOff)) == 0) {
			connection_set_radio(0);
		}
		printf("Dect radio event %s\n", strVal);
		return;
	}

	/* Handle event:
	 * ubus send dect '{ "registration": "off" }' */
	if(keys[STATE_REGISTRATION]) {
		strVal = blobmsg_get_string(keys[STATE_REGISTRATION]);
		if(strncmp(strVal, strOn, sizeof(strOn)) == 0) {
			connection_set_registration(1);
		}
		else if(strncmp(strVal, strOff, sizeof(strOff)) == 0) {
			connection_set_registration(0);
		}
		printf("Dect registration event %s\n", strVal);
		return;
	}
}



//-------------------------------------------------------------
static void ubus_fd_handler(void * dect_stream __attribute__((unused)), void * event __attribute__((unused))) {
	ubus_handle_event(ubusContext);
}



//-------------------------------------------------------------
// Tokenize RPC message key/value paris into an array
static int keyTokenize(struct ubus_object *obj, const char *methodName,
		struct blob_attr *msg, struct blob_attr ***keys)
{
	const struct ubus_method *search;

	// Find the ubus policy for the called method
	for(search = obj->methods; strcmp(search->name, methodName); search++);
	*keys = malloc(search->n_policy * sizeof(struct blob_attr*));
	if(!*keys) return UBUS_STATUS_INVALID_ARGUMENT;

	// Tokenize message into an array
	if(blobmsg_parse(search->policy, search->n_policy, *keys, 
			blob_data(msg), blob_len(msg))) {
		return UBUS_STATUS_INVALID_ARGUMENT;
	}

	return UBUS_STATUS_OK;
}



//-------------------------------------------------------------
// RPC handler for
// ubus call dect state '{....}'
static int ubus_request_state(struct ubus_context *ubus_ctx, struct ubus_object *obj,
		struct ubus_request_data *req, const char *methodName, struct blob_attr *msg)
{
	struct blob_attr **keys;
	const char *strVal;
	int res;

	// Tokenize message key/value paris into an array
	res = keyTokenize(obj, methodName, msg, &keys);
	if(res != UBUS_STATUS_OK) goto out;

	// Handle RPC:
	// ubus call dect state '{ "radio": "on" }'
	if(keys[STATE_RADIO]) {
		strVal = blobmsg_get_string(keys[STATE_RADIO]);
		if(strncmp(strVal, strOn, sizeof(strOn)) == 0) {
			if(connection_set_radio(1)) {
				ubus_reply_err(ubus_ctx, req, methodName, ENODEV);
			}
			else {
				ubus_reply_success(ubus_ctx, req, methodName);
			}
		}
		else if(strncmp(strVal, strOff, sizeof(strOff)) == 0) {
			connection_set_radio(0);
			ubus_reply_success(ubus_ctx, req, methodName);
		}
	}

	// Handle RPC:
	// ubus call dect state '{ "registration": "on" }'
	if(keys[STATE_REGISTRATION]) {
		strVal = blobmsg_get_string(keys[STATE_REGISTRATION]);
		if(strncmp(strVal, strOn, sizeof(strOn)) == 0) {
			if(connection_set_registration(1)) {
				ubus_reply_err(ubus_ctx, req, methodName, ENODEV);
			}
			else {
				ubus_reply_success(ubus_ctx, req, methodName);
			}
		}
		else if(strncmp(strVal, strOff, sizeof(strOff)) == 0) {
			connection_set_registration(0);
			ubus_reply_success(ubus_ctx, req, methodName);
		}
	}

	// Handle RPC:
	// ubus call dect state '{ "unused": 123 }'
	//if(keys[STATE_UNUSED]) {
	//	numVal = blobmsg_get_u32(keys[STATE_UNUSED]);
	//	printf("ronny state unused %d\n", numVal);
	//}

out:
	free(keys);
	return res;
}



//-------------------------------------------------------------
// RPC handler for
// ubus call dect handset '{....}'
static int ubus_request_handset(struct ubus_context *ubus_ctx, struct ubus_object *obj,
		struct ubus_request_data *req, const char *methodName, struct blob_attr *msg)
{
	struct blob_attr **keys;
	int res;

	// Tokenize message key/value paris into an array
	res = keyTokenize(obj, methodName, msg, &keys);
	if(res != UBUS_STATUS_OK) goto out;

	// Are we ready for another deferred request?
	perhaps_reply_deferred_timeout();
	if(querier.inUse) {
		ubus_reply_busy(ubus_ctx, req, methodName);
		res = UBUS_STATUS_NO_DATA;
		goto out;
	}

	// Handle RPC:
	// ubus call dect handset '{ "list": "" }'
	if(keys[HANDSET_LIST]) {
		if(list_handsets()) {
			ubus_reply_busy(ubus_ctx, req, methodName);
			res = UBUS_STATUS_NO_DATA;
		}
		else {
			querier.inUse = 1;
			querier.ubus_ctx = ubus_ctx;
			clock_gettime(CLOCK_MONOTONIC, &querier.timeStamp);
			ubus_defer_request(ubus_ctx, req, &querier.req);
		}
	}

	// ubus call dect handset '{ "delete": "" }'
	if(keys[HANDSET_DELETE] && 
			delete_handset(blobmsg_get_u32(keys[HANDSET_DELETE]))) {
		res = UBUS_STATUS_NO_DATA;
	}

	// ubus call dect handset '{ "pageall": "" }'
	if(keys[HANDSET_PAGEALL] && page_all_handsets()) {
		res = UBUS_STATUS_NO_DATA;
	}

out:
	free(keys);
	return res;
}



//-------------------------------------------------------------
// RPC handler for ubus call dect status
static int ubus_request_status(struct ubus_context *ubus_ctx, struct ubus_object *obj __attribute__((unused)), struct ubus_request_data *req, const char *methodName, struct blob_attr *msg __attribute__((unused)))
{
	char *keys, *key, *values, *value;
	char *saveptr1, *saveptr2;
	struct blob_buf blob;
	int res = UBUS_STATUS_OK;
	void *listRfpi;

	memset(&blob, 0, sizeof(blob));

	// Fetch status of radio etc.
	if(blobmsg_buf_init(&blob) || connection_get_status(&keys, &values)) {
		return UBUS_STATUS_NO_DATA;
	}
	
	// Add json strings for each status property
	key = keys;
	value = values;
	while((key = strtok_r(key, "\n", &saveptr1)) && 
			(value = strtok_r(value, "\n", &saveptr2))) {
		blobmsg_add_string(&blob, key, value);
		key = NULL;
		value = NULL;
	}

	// Add fixed part RFPI
	listRfpi = blobmsg_open_array(&blob, "rfpi");
	blobmsg_add_u16(&blob, NULL, connection.rfpi[0]);
	blobmsg_add_u16(&blob, NULL, connection.rfpi[1]);
	blobmsg_add_u16(&blob, NULL, connection.rfpi[2]);
	blobmsg_add_u16(&blob, NULL, connection.rfpi[3]);
	blobmsg_add_u16(&blob, NULL, connection.rfpi[4]);
	blobmsg_close_table(&blob, listRfpi);

	// Add returncode
	blobmsg_add_u32(&blob, "errno", 0);
	blobmsg_add_string(&blob, "errstr", strerror(0));
	blobmsg_add_string(&blob, "method", methodName);

	ubus_send_reply(ubus_ctx, req, blob.head);
	blob_buf_free(&blob);
	free(keys);
	free(values);

	return res;
}



//-------------------------------------------------------------
// RPC handler for
// ubus call dect call '{ "terminal": x, "add": x }'
static int ubus_request_call(struct ubus_context *ubus_ctx, struct ubus_object *obj,
		struct ubus_request_data *req, const char *methodName, struct blob_attr *msg)
{
	int res, termId, pcmId, add, release;
	struct blob_attr **keys;
	const char *cid;

	termId = -1;
	pcmId = -1;
	add = 0;
	release = 0;
	cid = NULL;

printf("Got incomming ubus request\n");
	// Tokenize message key/value paris into an array
	res = keyTokenize(obj, methodName, msg, &keys);
	if(res != UBUS_STATUS_OK) goto out;

	if(keys[CALL_TERM]) {
		termId = blobmsg_get_u32(keys[CALL_TERM]);
		printf("call term %d\n", termId);
	}

	if(keys[CALL_ADD]) {
		add = 1;
		pcmId = blobmsg_get_u32(keys[CALL_ADD]);
		printf("call add %d\n", pcmId);
	}

	if(keys[CALL_REL]) {
		release = 1;
		pcmId = blobmsg_get_u32(keys[CALL_REL]);
		printf("call release %d\n", pcmId);
	}

	if(keys[CALL_CID]) {
		cid = blobmsg_get_string(keys[CALL_CID]);
		printf("call cid %s\n", cid);
	}

	if(pcmId < 0 || pcmId > MAX_NR_PCM || (!add && !release)) {
		res = ubus_reply_err(ubus_ctx, req, methodName, EINVAL);
		return 0;
	}

	// Did we get all arguments we need?
	if(release) {
		if(release_req_async((uint32_t) termId, pcmId)) {
			res = ubus_reply_err(ubus_ctx, req, methodName, EINVAL);
		}
		else {
			res = ubus_reply_success(ubus_ctx, req, methodName);
		}
	}
	else if(add && termId >= 0) {
		if(setup_req((uint32_t) termId, pcmId, cid)) {
			res = ubus_reply_busy(ubus_ctx, req, methodName);
		}
		else {
			res = ubus_reply_success(ubus_ctx, req, methodName);
		}
	}
	else {
		res = ubus_reply_err(ubus_ctx, req, methodName, EINVAL);
	}
	
out:
	free(keys);
	return res;
}



//-------------------------------------------------------------
// When we know we are going to be busy for a long time
// and thus can't process received ubus messages - block
// them entirely. Then anyone trying to send us a message
// will fail immediately as opposed to timing out.
int ubus_disable_receive(void) {
	if(!isReceiveing) return 0;
	if(!ubusContext) return 0;

	if(ubus_remove_object(ubusContext, &rpcObj) != UBUS_STATUS_OK) {
		exit_failure("Error deregistering ubus object rpcObj");
	}

	if(ubus_unregister_event_handler(ubusContext, &buttonListener) != 
			UBUS_STATUS_OK) {
		exit_failure("Error deregistering ubus event handler %s", strButton);
	}

	if(ubus_unregister_event_handler(ubusContext, &stateListener) != 
			UBUS_STATUS_OK) {
		exit_failure("Error deregistering ubus event handler %s", ubusSenderPath);
	}

	if(querier.inUse) {
		ubus_complete_deferred_request(querier.ubus_ctx, &querier.req, 
			UBUS_STATUS_UNKNOWN_ERROR);
		querier.inUse = 0;
	}

	isReceiveing = 0;
	printf("UBUS receiver has been disabled\n");

	return 0;
}



//-------------------------------------------------------------
// Start accepting ubus messages (when we know we are
// ready for processing).
int ubus_enable_receive(void) {
	if(isReceiveing) return 0;
	isReceiveing = 1;

	/* Register event handler (not calls) for:
	 * ubus send dect '{ "key": "value" }' */
	memset(&stateListener, 0, sizeof(stateListener));
	stateListener.cb = ubus_event_state;
	if(ubus_register_event_handler(ubusContext, &stateListener,
			ubusSenderPath) != UBUS_STATUS_OK) {
		exit_failure("Error registering ubus event handler %s", ubusSenderPath);
	}

	/* Register event handler (not calls) for:
	 * ubus send button.DECT '{ "action": "pressed" }' */
	memset(&buttonListener, 0, sizeof(buttonListener));
	buttonListener.cb = ubus_event_button;
	if(ubus_register_event_handler(ubusContext, &buttonListener,
			strButton) != UBUS_STATUS_OK) {
		exit_failure("Error registering ubus event handler %s", strButton);
	}

	// Invoke our RPC handler when ubus calls (not events) arrive
	memset(&rpcType, 0, sizeof(rpcType));
	rpcType.name = ubusSenderPath;
	rpcType.n_methods = ARRAY_SIZE(ubusMethods);
	rpcType.methods = ubusMethods;
	memset(&rpcObj, 0, sizeof(rpcObj));
	rpcObj.name = ubusSenderPath;
	rpcObj.path = ubusSenderPath;
	rpcObj.type = &rpcType;
	rpcObj.methods = ubusMethods;
	rpcObj.n_methods = ARRAY_SIZE(ubusMethods);
	if(ubus_add_object(ubusContext, &rpcObj) != UBUS_STATUS_OK) {
		exit_failure("Error registering ubus object rpcObj");
	}

	if(ubus_lookup_id(ubusContext, "uci", &uciId) != UBUS_STATUS_OK) {
		exit_failure("Error, can't get UCI path");
	}

	printf("UBUS receiver has been enabled\n");

	return 0;
}



//-------------------------------------------------------------
void ubus_init(void *base __attribute__((unused)), config_t *config __attribute__((unused))) {
	printf("ubus init\n");
	memset(&querier, 0, sizeof(querier));

	isReceiveing = 0;
	ubusContext = ubus_connect(NULL);
	if(!ubusContext) exit_failure("Failed to connect to ubus");

	// Listen for data on ubus socket in our main event loop
	ubus_stream = stream_new(ubusContext->sock.fd);
	stream_add_handler(ubus_stream, 0, ubus_fd_handler);
	event_base_add_stream(ubus_stream);
}


//-------------------------------------------------------------
// For testing in factory production of new devices. In end
// customers units this function is a stub and does nothing.
int production_test_call_hanset(busmail_t *mail __attribute__((unused))) {
	return 0;
}

