
#ifndef EXTERNAL_CALL_H
#define EXTERNAL_CALL_H

#include <stdint.h>

int is_in_call(ApiTerminalIdType TerminalId);
int setup_req(uint32_t termId, int pcmId, const char *cid);
int asterisk_cfm(int pcmId, int err);
int release_req_async(uint32_t termId, int pcmId);
void external_call_init(void * bus);


#endif
