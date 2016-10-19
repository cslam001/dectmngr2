
/* Ability to build without ubus etc. for factory production.
 * This file is an empty API, for replacing ubus.c at link time. */


#include "ubus.h"
#include "util.h"
#include "dect.h"


//-------------------------------------------------------------
const char ubusSenderPath[] = "";
const char ubusStrActive[] = "";
const char ubusStrInActive[] = "";



//-------------------------------------------------------------

int ubus_send_strings(const char *path __attribute__((unused)), const char *msgKey[] __attribute__((unused)), const char *msgVal[] __attribute__((unused)), int len __attribute__((unused))) {
	return 0;
}

int ubus_send_string_to(const char *path __attribute__((unused)), const char *msgKey __attribute__((unused)), const char *msgVal __attribute__((unused))) {
	return 0;
}

int ubus_send_string(const char *msgKey __attribute__((unused)), const char *msgVal __attribute__((unused))) {
	return 0;
}

int ubus_call_string(const char *path __attribute__((unused)), const char* method __attribute__((unused)), const char *key __attribute__((unused)), 
		const char *val __attribute__((unused)), ubus_call_complete_callback cb __attribute__((unused))) {
	return 0;
}

int uci_call_query(const char *option __attribute__((unused))) {
	return -1;
}

int asterisk_call(int terminal __attribute__((unused)), int add __attribute__((unused)), int release __attribute__((unused)), const char *cid __attribute__((unused))) {
	return 0;
}

int ubus_reply_handset_list(int retErrno __attribute__((unused)), const struct handsets_t const *handsets __attribute__((unused))) {
	return 0;
}

int ubus_disable_receive(void) {
	return 0;
}

int ubus_enable_receive(void) {
	return 0;
}

void ubus_init(void * base __attribute__((unused)), config_t * config __attribute__((unused))) {};



