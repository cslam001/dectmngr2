
#ifndef CONNECTION_INIT_H
#define CONNECTION_INIT_H


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

static const char *remote_bool_str[] = {				// String explanations of enum remote_bool_t
	"unknown",
	"pending active",
	"active",
	"pending inactive",
	"inactive",
};


struct connection_t {
	enum remote_bool_t registration;
	enum remote_bool_t radio;
	enum uciRadioConf_t uciRadioConf;					// Should radio be always on/off/auto=
};


struct connection_t connection;


int connection_get_status(char **keys, char **values);
void connection_init(void * bus);
int connection_set_radio(int onff);
int connection_set_registration(int onoff);
int start_internal_dect(void);

#endif
