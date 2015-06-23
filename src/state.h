
#include <stdint.h>
#include "util.h"

enum states {
	BOOT_STATE,
	PRELOADER_STATE,
	FLASHLOADER_STATE,
	APP_STATE,
	NVS_STATE,
	TEST_STATE,
};




struct state_handler {
	int state;
	void (*init_state)(int fd, config_t * c);
	void (*event_handler)(void * event);
};


void state_add_handler(struct state_handler *s, int fd);
void * state_get_handler(void);

extern struct state_handler * preloader_state;
extern struct state_handler * flashloader_state;

