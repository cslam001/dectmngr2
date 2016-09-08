
#ifndef CONNECTION_INIT_H
#define CONNECTION_INIT_H

#include <stdint.h>


enum uciRadioConf_t {
	RADIO_ALWAYS_OFF,
	RADIO_ALWAYS_ON,
	RADIO_AUTO
};


enum remote_bool_t {
	UNKNOWN,
	PENDING_ACTIVE,										// We have sent a request to activate something
	ACTIVE,												// Confirmed response, it's active
	PENDING_INACTIVE,
	INACTIVE,
};

struct connection_t {
	enum remote_bool_t registration;
	enum remote_bool_t radio;
	enum uciRadioConf_t uciRadioConf;					// Should radio be always on/off/auto
	uint8_t rfpi[5];									// Fixed part RFPI
	char fwVersion[16];									// Dect stack version
	char type[16];										// Country spectrum
};


struct connection_t connection;							// Connection to fixed part (base)


int connection_get_status(char **keys, char **values);
void connection_init(void * bus);
int connection_set_radio(int onff);
int connection_set_registration(int onoff);
int start_internal_dect(void);
int perhaps_disable_radio(void);

#endif
