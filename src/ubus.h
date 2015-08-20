
#ifndef DECTMNGR_UBUS_H
#define DECTMNGR_UBUS_H

#include "util.h"


extern const char ubusStrActive[];
extern const char ubusStrInActive[];


int ubus_send_string(const char *msgKey, const char *msgVal);
void ubus_init(void * base, config_t * config);

#endif


