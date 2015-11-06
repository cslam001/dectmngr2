#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include "dect.h"
#include "util.h"
#include "boot.h"
#include "app.h"
#include "nvs.h"
#include "boot.h"
#include <board.h>

#include <Api/RsStandard.h>


static struct timespec timeStartup;								// Time stamp of when application starts up



void print_status(RsStatusType s) {

	switch (s) {
		
	case RSS_SUCCESS:
		printf("RSS_SUCCESS\n");
		break;

	case RSS_NOT_SUPPORTED:
		printf("RSS_NOT_SUPPORTED\n");
		break;

	case RSS_BAD_ARGUMENTS:
		printf("RSS_BAD_ARGUMENTS\n");
		break;
		
	default:
		printf("STATUS: %x\n", s);
	}
}


void util_dump(unsigned char *buf, int size, char *start) {

	int i, dumpsize;
	int maxdump = 10000;
	//unsigned char* cdata = (unsigned char*)buf;

	printf("%s", start);
	printf("[%04d] - ",size);
    
	if ( size > maxdump ) {
		dumpsize = maxdump; 
	} else {
		dumpsize = size; 
	}

	for (i=0 ; i<dumpsize ; i++) {
		printf("%02x ",buf[i]);
	}
	printf("\n");
	
}


static void print_usage(const char * name) {

	printf("Usage: %s [--app] [--prog] [--nvs]\n", name);
	printf("\t[--help] [--rsx]\n\n");

	printf("\tapp\t: Start in application mode\n");
	printf("\tprog\t: Program DECT flash chip\n");
	printf("\tnvs\t: Configure DECT chip\n");
	printf("\thelp\t: print this help and exit\n\n");
	printf("\trsx\t: Wait for RSX debug connection\n\n");
}


int check_args(int argc, char * argv[], config_t * c) {

	int arg, count = 0;

	const char * short_opt = "hpant:";
	struct option long_opt[] = {
		{"help", no_argument, NULL, 'h'},
		{"prog", no_argument, NULL, 'p'},
		{"app", no_argument, NULL, 'a'},
		{"nvs", no_argument, NULL, 'n'},
		{"rsx", no_argument, NULL, 'r'},
		{NULL, 0, NULL, 0}
	};

	while (( arg = getopt_long(argc, argv, short_opt, long_opt, NULL)) != -1) {
		
		switch (arg) {
		case -1:
		case 0:
			break;

		case 'a':
			c->mode = APP_MODE;
			break;

		case 'n':
			c->mode = NVS_MODE;
			break;

		case 'p':
			c->mode = PROG_MODE;
			break;

		case 'r':
			c->wait_dbg_client = 1;
			break;

		case 'h':
			print_usage(argv[0]);
			return -1;

		default:
			print_usage(argv[0]);
			return -1;
		}
		
		count++;
	}
	
	if ( count > 0 ) {
		
		return 0;

	} else {

		/* No arguments provided */
		print_usage(argv[0]);
		return -1;
	}
}


int gpio_control(int gpio, int state) {
	BOARD_IOCTL_PARMS IoctlParms;
	IoctlParms.string = 0;
	IoctlParms.strLen = gpio;
	IoctlParms.offset = state;
	IoctlParms.action = 0;
	IoctlParms.buf = 0;
	int boardFd;
	
	boardFd = open("/dev/brcmboard", O_RDWR);
	if (boardFd == -1 ) {
		perror("failed to open: /dev/brcmboard");
		return -1;
	}
    
	if ( ioctl(boardFd, BOARD_IOCTL_SET_GPIO, &IoctlParms) < 0 ) {
		perror("ioctl set gpio failed");
		return -1;
	}

	close(boardFd);

    return 0;
}


int dect_chip_reset(void) {
	if(gpio_control(3, 1)) return -1;
	usleep(100000);								// Wait for chip to reset
	if(gpio_control(3, 0)) return -1;
	usleep(200000);								// Wait while chip starts up
	
	return 0;
}


// Initialize the time stamp measurement for how
// long this application has been up and running.
int timeSinceInit(void) {
	if(clock_gettime(CLOCK_MONOTONIC, &timeStartup)) {
		perror("Error getting time");
		return -1;
	}

	return 0;
}


// Return number of micro seconds since this
// application started up.
int64_t timeSinceStart(void) {
	struct timespec now;
	long t;

	if(clock_gettime(CLOCK_MONOTONIC, &now)) {
		perror("Error getting time");
		return -1;
	}

	if(now.tv_nsec >= timeStartup.tv_nsec) {
		t = now.tv_nsec - timeStartup.tv_nsec;
	}
	else {
		t = 1e9L - timeStartup.tv_nsec + now.tv_nsec;
	}

	return (int64_t) ((now.tv_sec - timeStartup.tv_sec) * 1e6L) +
		(int64_t) (t / 1e3L);
}

