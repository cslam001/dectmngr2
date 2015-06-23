#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>

#include "dect.h"
#include "boot.h"
#include "util.h"

static uint8_t PreBootPrgm441_20MHz[] =
	{
#include "../files/PreLoader441_20MHZ.csv"
	};


#define BUF_SIZE 500

static struct bin_img preloader;
static struct bin_img *pr = &preloader;


static int dect_fd;



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
			
			/* Transition to preloader state */
			boot_exit(stream);
			preloader_init(stream);

		} else {
			printf("Unknown boot packet: %x\n", in[0]);
		}
		break;
	}
}


void boot_init(void * stream) {

	printf("boot_init\n");
	stream_add_handler(stream, boot_handler);
	dect_fd = stream_get_fd(stream);

	read_preloader();
	calculate_checksum();
	
	printf("DECT TX PULLDOWN\n");
	if(gpio_control(118, 1)) return;

	printf("RESET_DECT\n");
	if(dect_chip_reset()) return;

	printf("DECT TX TO BRCM RX\n");
	if(gpio_control(118, 0)) return;
	
}


void boot_exit(void * stream) {

	printf("boot_exit\n");
	stream_remove_handler(stream, boot_handler);
}
