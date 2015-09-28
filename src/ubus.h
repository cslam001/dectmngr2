
#ifndef DECTMNGR_UBUS_H
#define DECTMNGR_UBUS_H

#include <time.h>
#include <libubus.h>

#include "util.h"
#include "handset.h"

//-------------------------------------------------------------
struct querier_t {
	int inUse;												// Free or busy?
	struct timespec timeStamp;								// Timestamp of request
	struct ubus_request_data req;
	struct ubus_context *ubus_ctx;
};


extern const char ubusStrActive[];
extern const char ubusStrInActive[];


typedef void (*ubus_call_complete_callback)(int ret);


//-------------------------------------------------------------
int ubus_send_string(const char *msgKey, const char *msgVal);
int ubus_send_string_api(const char *sender, const char *msgKey, const char *msgVal);
int ubus_send_json_string(const char *sender_id, const char *json_string);
int ubus_call_string(const char *path, const char* method, const char *key, 
	const char *val, ubus_call_complete_callback cb);
int ubus_reply_handset_list(int retErrno, const struct handsets_t const *handsets);
void ubus_init(void * base, config_t * config);

#endif


