
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


extern const char ubusSenderPath[];
extern const char ubusStrActive[];
extern const char ubusStrInActive[];

typedef void (*ubus_call_complete_callback)(int ret);


//-------------------------------------------------------------
int ubus_send_strings(const char *path, const char *msgKey[], const char *msgVal[], int len);
int ubus_send_string_to(const char *path, const char *msgKey, const char *msgVal);
int ubus_send_string(const char *msgKey, const char *msgVal);
int ubus_call_string(const char *path, const char* method, const char *key, 
	const char *val, ubus_call_complete_callback cb);
int uci_call_query(const char *option);
int asterisk_call(int terminal, int add, int release, const char *cid);
int ubus_reply_handset_list(int retErrno, const struct handsets_t const *handsets);
int ubus_disable_receive(void);
int ubus_enable_receive(void);
void ubus_init(void * base, config_t * config);

#endif


