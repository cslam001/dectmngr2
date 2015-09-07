#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <libubus.h>
#include <libubox/blobmsg_json.h>
#include <json-c/json_object.h>
#include <json-c/json_util.h>
#include <json-c/json_tokener.h>

#include "ubus.h"
#include "util.h"
#include "error.h"
#include "stream.h"
#include "event_base.h"
#include "connection_init.h"


//-------------------------------------------------------------
enum {
	STATE_RADIO,
	STATE_REGISTRATION,
	STATE_UNUSED,
};

enum {
	HANDSET_LIST,
	HANDSET_DELETE,
	HANDSET_PAGEALL,															// Page (ping) all handsets
};

static int ubus_request_state(struct ubus_context *ubus_ctx, struct ubus_object *obj,
		struct ubus_request_data *req, const char *methodName, struct blob_attr *msg);
static int ubus_request_handset(struct ubus_context *ubus_ctx, struct ubus_object *obj,
		struct ubus_request_data *req, const char *methodName, struct blob_attr *msg);
static int ubus_request_status(struct ubus_context *ubus_ctx, struct ubus_object *obj,
		struct ubus_request_data *req, const char *methodName, struct blob_attr *msg);



//-------------------------------------------------------------
static const char ubusSenderId[] = "dect";										// The UBUS type we transmitt
const char ubusStrActive[] = "active";											// The "bool" we transmitt for service is on/enabled/operational
const char ubusStrInActive[] = "inactive";
const char strOn[] = "on";
const char strOff[] = "off";
const char strAction[] = "action";
const char strPressed[] = "pressed";											// Button pressed string
const char strReleased[] = "released";


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


static const struct ubus_method ubusMethods[] = {								// ubus RPC methods
	UBUS_METHOD("state", ubus_request_state, ubusStateKeys),
	UBUS_METHOD("handset", ubus_request_handset, ubusHandsetKeys),
	UBUS_METHOD_NOARG("status", ubus_request_status),
};


static struct ubus_object_type rpcType[] = {
	UBUS_OBJECT_TYPE(ubusSenderId, ubusMethods)
};


static struct ubus_object rpcObj = {
	.name = ubusSenderId,
	.type = rpcType,
	.methods = ubusMethods,
	.n_methods = ARRAY_SIZE(ubusMethods)
};


static struct ubus_context *ubusContext;
static void *ubus_stream;
static struct ubus_event_handler listener;										// Event handler registration
static struct querier_t querier;												// Handle to deferred RPC request


//-------------------------------------------------------------
// We transmitt a public ubus string event
int ubus_send_string(const char *msgKey, const char *msgVal)
{
	struct blob_buf blob;
	int res = 0;

	memset(&blob, 0, sizeof(blob));
	if(blob_buf_init(&blob, 0)) return -1;
	blobmsg_add_string(&blob, msgKey, msgVal);

	if(ubus_send_event(ubusContext, ubusSenderId, blob.head) != UBUS_STATUS_OK) {
		printf("Error sending ubus message %s %s\n", msgKey, msgVal);
			res = -1;
	}

	blob_buf_free(&blob);

	return res;
}


//-------------------------------------------------------------
// Callback for: a ubus call (invocation) has replied with some data
static void call_answer(struct ubus_request *req, int type, struct blob_attr *msg)
{
}


//-------------------------------------------------------------
// Callback for: a ubus call (invocation) has finished
static void call_complete(struct ubus_request *req, int ret)
{
	ubus_call_complete_callback cb = (ubus_call_complete_callback) req->priv;

	if(cb) cb(ret);
	free(req);
}


//-------------------------------------------------------------
// We invoke someone with ubus RPC and a string argument
int ubus_call_string(const char *path, const char* method, const char *key, 
	const char *val, ubus_call_complete_callback cb)
{
	struct ubus_request *req;
	struct blob_buf blob;
	uint32_t id;
	int res;

	res = 0;

	req = calloc(1, sizeof(struct ubus_request));
	if(!req) return -1;

	memset(&blob, 0, sizeof(blob));
	if(blob_buf_init(&blob, 0)) return -1;
	blobmsg_add_string(&blob, key, val);

	// Find id number for ubus "path"
	res = ubus_lookup_id(ubusContext, path, &id);
	if(res != UBUS_STATUS_OK) {
		printf("Error searching for usbus path\n");
		res = -1;
		goto out;
	}

	/* Call remote method, without
	 * waiting for completion. */
	res = ubus_invoke_async(ubusContext, id, method, blob.head, req);
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
	if(res == -1) free(req);
	blob_buf_free(&blob);

	return res;
}


//-------------------------------------------------------------
// Send a busy reply that a request was successful
static int ubus_reply_success(struct ubus_context *ubus_ctx, struct ubus_object *obj,
		struct ubus_request_data *req, const char *methodName, struct blob_attr *msg)
{
	struct blob_buf isOkMsg;

	memset(&isOkMsg, 0, sizeof(isOkMsg));
	if(blobmsg_buf_init(&isOkMsg)) return -1;

	blobmsg_add_u32(&isOkMsg, "errno", 0);
	blobmsg_add_string(&isOkMsg, "errstr", strerror(0));
	blobmsg_add_string(&isOkMsg, "method", methodName);
	blobmsg_add_blob(&isOkMsg, msg);

	ubus_send_reply(ubus_ctx, req, isOkMsg.head);
	blob_buf_free(&isOkMsg);

	return 0;
}

//-------------------------------------------------------------
// Send a busy reply that "we can't handle caller
// request at the moment. Please retry later."
static int ubus_reply_busy(struct ubus_context *ubus_ctx, struct ubus_object *obj,
		struct ubus_request_data *req, const char *methodName, struct blob_attr *msg)
{
	struct blob_buf isBusyMsg;

	memset(&isBusyMsg, 0, sizeof(isBusyMsg));
	if(blobmsg_buf_init(&isBusyMsg)) return -1;

	blobmsg_add_u32(&isBusyMsg, "errno", EBUSY);
	blobmsg_add_string(&isBusyMsg, "errstr", strerror(EBUSY));
	blobmsg_add_string(&isBusyMsg, "method", methodName);
	blobmsg_add_blob(&isBusyMsg, msg);

	ubus_send_reply(ubus_ctx, req, isBusyMsg.head);
	blob_buf_free(&isBusyMsg);

	return 0;
}



//-------------------------------------------------------------
// Send a list of registered phones as reply
int ubus_reply_handset_list(int retErrno, const struct handsets_t const *handsets)
{
	struct blob_buf blob;
	void *list1, *tbl, *list2;
	int i;

	if(!querier.inUse) return -1;

	memset(&blob, 0, sizeof(blob));
	if(blobmsg_buf_init(&blob)) return -1;

	list1 = blobmsg_open_array(&blob, "handsets");
	for(i = 0; i < handsets->termCount; i++) {
		tbl = blobmsg_open_table(&blob, NULL);
		blobmsg_add_u32(&blob, "id", handsets->terminal[i].id);

		list2 = blobmsg_open_array(&blob, "ipui");
		blobmsg_add_u16(&blob, NULL, handsets->terminal[i].ipui[0]);
		blobmsg_add_u16(&blob, NULL, handsets->terminal[i].ipui[1]);
		blobmsg_add_u16(&blob, NULL, handsets->terminal[i].ipui[2]);
		blobmsg_add_u16(&blob, NULL, handsets->terminal[i].ipui[3]);
		blobmsg_add_u16(&blob, NULL, handsets->terminal[i].ipui[4]);
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

	if(now.tv_sec - querier.timeStamp.tv_sec > 4) {
		ubus_complete_deferred_request(querier.ubus_ctx, &querier.req, 
			UBUS_STATUS_TIMEOUT);
		querier.inUse = 0;
	}
}



//-------------------------------------------------------------
// Event handler for
// ubus send button.DECT '{ "action": "pressed" }'
static void ubus_event_button(struct ubus_context *ctx,
	struct ubus_event_handler *ev, const char *type, struct blob_attr *blob)
{
	struct json_object *obj, *val;
	char *json;

// THERE IS A MEMORY LEAK BELOW!!!
// PROBABLY WE CAN REMOVE THE json
// REFORMATION AND USE blobmsg_get_string()
	json = blobmsg_format_json(blob, true);										// Blob to JSON
	obj = json_tokener_parse(json);												// JSON to C-struct

	if(!json_object_object_get_ex(obj, strAction, &val)) return;

	if(json_object_get_type(val) == json_type_string) {							// Primitive type of value for key?
		if(strncmp(json_object_get_string(val), strPressed, 
				sizeof(strPressed)) == 0) {
			printf("Dect button pressed\n");
			connection_set_registration(1);
		}
		else if(strncmp(json_object_get_string(val), strReleased,
				sizeof(strReleased)) == 0) {
			printf("Dect button released\n");
			connection_set_radio(0);
		}
	}

	free(json);
}



//-------------------------------------------------------------
// Subevent handler for
// ubus send dect '{ "registration": "off" }'
static int ubus_event_registration(struct json_object *val)
{
	switch(json_object_get_type(val)) {											// Primitive type of value for key?
		case json_type_string:
			if(strncmp(json_object_get_string(val), strOn, sizeof(strOn)) == 0) {
				connection_set_registration(1);
			}
			else if(strncmp(json_object_get_string(val), strOff, sizeof(strOff)) == 0) {
				connection_set_registration(0);
			}
			break;

		case json_type_boolean:
			connection_set_registration(json_object_get_boolean(val));
			break;

		case json_type_int:
			connection_set_registration(json_object_get_int(val));
			break;
		
		default:
			return -1;
	}

	return 0;
}



//-------------------------------------------------------------
// Subevent handler for
// ubus send dect '{ "radio": "off" }'
static int ubus_event_radio(struct json_object *val)
{
	switch(json_object_get_type(val)) {											// Primitive type of value for key?
		case json_type_string:
			if(strncmp(json_object_get_string(val), strOn, sizeof(strOn)) == 0) {
				connection_set_radio(1);
			}
			else if(strncmp(json_object_get_string(val), strOff, sizeof(strOff)) == 0) {
				connection_set_radio(0);
			}
			break;

		case json_type_boolean:
			connection_set_radio(json_object_get_boolean(val));
			break;

		case json_type_int:
			connection_set_radio(json_object_get_int(val));
			break;
		
		default:
			return -1;
	}

	return 0;
}



//-------------------------------------------------------------
// Event handler for
// ubus send dect '{ "key": "value" }'
static void ubus_event_state(struct ubus_context *ctx,
	struct ubus_event_handler *ev, const char *type, struct blob_attr *blob)
{
	struct json_object *obj, *val;
	char *json;
	int res;

	json = blobmsg_format_json(blob, true);										// Blob to JSON
	obj = json_tokener_parse(json);												// JSON to C-struct
	res = 0;

	if(json_object_object_get_ex(obj, ubusStateKeys[STATE_RADIO].name, &val)) {	// Contains this key?
		res = ubus_event_radio(val);
	}
	else if(json_object_object_get_ex(obj, 
			ubusStateKeys[STATE_REGISTRATION].name, &val)) {
		res = ubus_event_registration(val);
	}

	if(res) printf("Error for event %s\n", json_object_to_json_string(obj));

	free(json);
}



//-------------------------------------------------------------
static void ubus_fd_handler(void * dect_stream, void * event) {
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
			connection_set_radio(1);
			ubus_reply_success(ubus_ctx, obj, req, methodName, msg);
		}
		else if(strncmp(strVal, strOff, sizeof(strOff)) == 0) {
			connection_set_radio(0);
			ubus_reply_success(ubus_ctx, obj, req, methodName, msg);
		}
	}

	// Handle RPC:
	// ubus call dect state '{ "registration": "on" }'
	if(keys[STATE_REGISTRATION]) {
		strVal = blobmsg_get_string(keys[STATE_REGISTRATION]);
		if(strncmp(strVal, strOn, sizeof(strOn)) == 0) {
			connection_set_registration(1);
			ubus_reply_success(ubus_ctx, obj, req, methodName, msg);
		}
		else if(strncmp(strVal, strOff, sizeof(strOff)) == 0) {
			connection_set_registration(0);
			ubus_reply_success(ubus_ctx, obj, req, methodName, msg);
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
		ubus_reply_busy(ubus_ctx, obj, req, methodName, msg);
		res = UBUS_STATUS_NO_DATA;
		goto out;
	}

	// Handle RPC:
	// ubus call dect handset '{ "list": "" }'
	if(keys[HANDSET_LIST]) {
		if(list_handsets()) {
			ubus_reply_busy(ubus_ctx, obj, req, methodName, msg);
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
static int ubus_request_status(struct ubus_context *ubus_ctx, struct ubus_object *obj,
		struct ubus_request_data *req, const char *methodName, struct blob_attr *msg)
{
	char *keys, *key, *values, *value;
	char *saveptr1, *saveptr2;
	struct blob_buf blob;
	int res = UBUS_STATUS_OK;

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
void ubus_init(void * base, config_t * config) {	
	printf("ubus init\n");
	memset(&querier, 0, sizeof(querier));

	ubusContext = ubus_connect(NULL);
	if(!ubusContext) exit_failure("Failed to connect to ubus");

	// Listen for data on ubus socket in our main event loop
	ubus_stream = stream_new(ubusContext->sock.fd);
	stream_add_handler(ubus_stream, 0, ubus_fd_handler);
	event_base_add_stream(ubus_stream);

	/* Register event handler (not calls) for:
	 * ubus send dect '{ "key": "value" }' */
	memset(&listener, 0, sizeof(listener));
	listener.cb = ubus_event_state;
	if(ubus_register_event_handler(ubusContext, &listener, ubusSenderId) != 
			UBUS_STATUS_OK) {
		exit_failure("Error registering ubus event handler %s", ubusSenderId);
	}

	/* Register event handler (not calls) for:
	 * ubus send button.DECT '{ "action": "pressed" }' */
	listener.cb = ubus_event_button;
	if(ubus_register_event_handler(ubusContext, &listener, "button.DECT") != 
			UBUS_STATUS_OK) {
		exit_failure("Error registering ubus event handler button.DECT");
	}

	// Invoke our RPC handler when ubus calls (not events) arrive
	if(ubus_add_object(ubusContext, &rpcObj) != UBUS_STATUS_OK) {
		exit_failure("Error registering ubus object");
	}
}


