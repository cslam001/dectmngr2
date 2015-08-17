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
#include "util.h"
#include "error.h"
#include "stream.h"
#include "event_base.h"


static struct ubus_context *ubusContext;
static void *ubus_stream;
static void *event_base;
static struct ubus_event_handler listener;




//-------------------------------------------------------------
static int ubus_event_registration(struct json_object *val)
{
	switch(json_object_get_type(val)) {											// Primitive type of value for key?
		case json_type_string:
			if(strncmp(json_object_get_string(val), "on", sizeof("on")) == 0) {
				connection_set_registration(1);
			}
			else if(strncmp(json_object_get_string(val), "off", sizeof("off")) == 0) {
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
static int ubus_event_radio(struct json_object *val)
{
	switch(json_object_get_type(val)) {											// Primitive type of value for key?
		case json_type_string:
			if(strncmp(json_object_get_string(val), "on", sizeof("on")) == 0) {
				connection_set_radio(1);
			}
			else if(strncmp(json_object_get_string(val), "off", sizeof("off")) == 0) {
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
// Main handler for ubus events
static void ubus_event_handler(struct ubus_context *ctx,
	struct ubus_event_handler *ev, const char *type, struct blob_attr *blob)
{
	struct json_object *obj, *val;
	char *json;
	int res;

	json = blobmsg_format_json(blob, true);										// Blob to JSON
	obj = json_tokener_parse(json);												// JSON to C-struct
	res = 0;

	if(json_object_object_get_ex(obj, "radio", &val)) {							// Contains key?
		res = ubus_event_radio(val);
	}
	else if(json_object_object_get_ex(obj, "registration", &val)) {
		res = ubus_event_registration(val);
	}

	if(res) printf("Error for event %s\n", json_object_to_json_string(obj));

	free(json);
}



//-------------------------------------------------------------
void ubus_fd_handler(void * dect_stream, void * event) {
	ubus_handle_event(ubusContext);
}



//-------------------------------------------------------------
void ubus_init(void * base, config_t * config) {	
	int dect_fd, debug_fd, proxy_fd;

	printf("ubus init\n");
	event_base = base;

	ubusContext = ubus_connect(NULL);
	if (!ubusContext) exit_failure("Failed to connect to ubus");

	// Listsen for data on ubus socket
	ubus_stream = stream_new(ubusContext->sock.fd);
	stream_add_handler(ubus_stream, 0, ubus_fd_handler);
	event_base_add_stream(event_base, ubus_stream);

	// Invoke our handler when ubus events arrive
	memset(&listener, 0, sizeof(listener));
	listener.cb = ubus_event_handler;
	if(ubus_register_event_handler(ubusContext, &listener, "dect")) {
		exit_failure("Error registering ubus event handler");
	}

	return;
}


