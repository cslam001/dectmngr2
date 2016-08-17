#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


#include "main.h"
#include "dect.h"
#include "tty.h"
#include "error.h"
#include "boot.h"

#include "busmail.h"
#include "stream.h"
#include "event.h"
#include "event_base.h"

#include "prog.h"
#include "app.h"
#include "nvs.h"

#ifdef WITH_UBUS
#include "ubus.h"
#endif


#define MAX_EVENTS 10

int main(int argc, char * argv[]) {

	/* Unbuffered stdout */
	setbuf(stdout, NULL);
	
	event_base_new(MAX_EVENTS);
	
	/* Check user arguments and init config */
	if ( check_args(argc, argv, &config) < 0 ) {
		exit(EXIT_FAILURE);
	}

	if(timeSinceInit()) exit(EXIT_FAILURE);

	/* Select operating mode */
	switch (config.mode) {
	case PROG_MODE:
		prog_init(0, &config);
		break;
	case NVS_MODE:
		nvs_init(0, &config);
		break;
	case APP_MODE:
		app_init(0, &config);
#ifdef WITH_UBUS
		ubus_init(0, &config);
#endif
		break;
	default:
		err_exit("No known operating mode selected\n");
	}


	/* Read incomming events on registered streams
	   and dispatch them to event handlers. Does 
	   not return. */
	event_base_dispatch();

	return 0;
}
