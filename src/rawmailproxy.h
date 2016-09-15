
#ifndef RAWMAILPROXY_H
#define RAWMAILPROXY_H

#include "dect.h"


struct proxy_packet {
	uint32_t size;
	uint32_t type;
	uint8_t data[MAX_MAIL_SIZE];
} __attribute__((__packed__));



int hasProxyClient(void);
int rawmailproxy_init(void *dect_bus);




#endif // RAWMAILPROXY_H

