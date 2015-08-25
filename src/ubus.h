
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



//-------------------------------------------------------------
int ubus_send_string(const char *msgKey, const char *msgVal);
int ubus_reply_handset_list(int retErrno, const struct handsets_t const *handsets);
void ubus_init(void * base, config_t * config);

#endif


