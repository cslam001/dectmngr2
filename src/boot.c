
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/epoll.h>
#include <termios.h>

#include "dect.h"
#include "tty.h"
#include "error.h"
#include "state.h"
#include "boot.h"
#include "util.h"


static uint8_t PreBootPrgm441_20MHz[] =
	{
#include "../files/PreLoader441_20MHZ.csv"
	};


#define BUF_SIZE 500

static struct bin_img preloader;
static struct bin_img *pr = &preloader;


int dect_fd;



static void read_preloader(void) {

	int fd, size, ret, sz_ht;
	struct stat s;

	pr->size = sizeof(PreBootPrgm441_20MHz);
	pr->size_msb = (uint8_t) (pr->size >> 8);
	pr->size_lsb = (uint8_t) pr->size;

	printf("size: %d\n", pr->size);
  
	pr->img = malloc(pr->size);
  
	memcpy(pr->img, PreBootPrgm441_20MHz, pr->size);

}




static void calculate_checksum(void) {
  
	uint8_t chk=0;
	int i;
	uint8_t * FlashLoaderCodePtr = pr->img;

	// Calculate Checksum of flash loader
	for (i=0; i< pr->size; i++) {
		chk^=FlashLoaderCodePtr[i];
	}

	pr->checksum = chk;
	printf("checksum: %x\n", pr->checksum);
}


static void send_size(uint8_t * in) {

	uint8_t r[3];
	/* Reply */
	r[0] = SOH;
	r[1] = pr->size_lsb;
	r[2] = pr->size_msb;

	printf("SOH\n");
	util_dump(r, 3, "[WRITE]");
	write(dect_fd, r, 3);
}


static void send_preloader(uint8_t * in) {
  
	printf("WRITE_PRELOADER\n");
	write(dect_fd, pr->img, pr->size);
}

static void send_ack(uint8_t * in) {

	uint8_t c[3];

	c[0] = ACK;

	util_dump(c, 1, "[WRITE]");
	write(dect_fd, c, 1);
}


void boot_handler(void * stream, void * event) {

	uint8_t * in = (uint8_t *) event_data(event);
	
	switch (in[0]) {

	case SOH:
		printf("SOH\n");
		break;
	case STX:
		printf("STX\n");
		send_size(in);
		break;
	case ETX:
		printf("ETX\n");
		break;
	case ACK:
		printf("ACK\n");
		send_preloader(in);
		break;
	case NACK:
		printf("NACK\n\n");
		break;
	default:
		if (in[0] == pr->checksum) {
			printf("Checksum ok!\n");
			
			send_ack(in);

			/* make this prettier */
			/* state_add_handler(preloader_state, e->fd); */
			/* state_transition(PRELOADER_STATE); */

		} else {
			printf("Unknown boot packet: %x\n", in[0]);
		}
		break;
	}
}


void init_boot_state(void * event_base, config_t * config) {
	
	void * boot_stream;
	printf("BOOT_STATE\n");

	/* Setup dect tty */
	dect_fd = tty_open("/dev/ttyS1");
	tty_set_raw(dect_fd);
	tty_set_baud(dect_fd, B19200);
	
	/* Register dect stream */
	boot_stream = (void *) stream_new(dect_fd);
	stream_add_handler(boot_stream, boot_handler);
	event_base_add_stream(event_base, boot_stream);

	read_preloader();
	calculate_checksum();
	
	printf("DECT TX PULLDOWN\n");
	if(gpio_control(118, 1)) return;

	printf("RESET_DECT\n");
	if(dect_chip_reset()) return;

	printf("DECT TX TO BRCM RX\n");
	if(gpio_control(118, 0)) return;

}







struct state_handler boot_state_handler = {
	.state = BOOT_STATE,
	.init_state = init_boot_state,
};

struct state_handler * boot_state = &boot_state_handler;
