
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
#include "preloader.h"


static uint8_t FlashLoader441[] =
	{
#include "../files/FlashLoader441.csv"
	};


#define BUF_SIZE 500

static struct bin_img flashloader;
static struct bin_img *pr = &flashloader;
static int dect_fd;

//--------------------------------------------------------------------------
//     PC                            TARGET
//     ==                            ======
//             PRELOADER_START
//    --------------------------------->
//             PRELOADER_READY
//    <---------------------------------
//           PRELOADER_BAUD_xxxx
//    --------------------------------->
//         PRELOADER_NEW_BAUDRATE
//    --------------------------------->
//       PRELOADER_NEW_BAUDRATE_READY
//    <---------------------------------
//             msb code length
//    --------------------------------->
//            lsb code length
//    --------------------------------->
//                 code
//    --------------------------------->
//                 ....
// 			       ....
//                 code
//    --------------------------------->
//                 chk
//    <---------------------------------
//
//     Boot loader down loaded now
//
//--------------------------------------------------------------------------




static void read_flashloader(void) {

	int fd, size, ret, sz_ht;
	struct stat s;

  
	pr->size = sizeof(FlashLoader441);
	pr->size_msb = (uint8_t) (pr->size >> 8);
	pr->size_lsb = (uint8_t) pr->size;

	printf("size: %d\n", pr->size);
	pr->img = malloc(pr->size);
	memcpy(pr->img, FlashLoader441, pr->size);
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

	uint8_t c[2];

	/* Reply */
	c[0] = pr->size_msb;
	c[1] = pr->size_lsb;

	util_dump(c, 2, "[WRITE]");
	write(dect_fd, c, 2);
}


static void send_flashloader(uint8_t * in) {
  
	printf("WRITE_FLASHLOADER: %d\n", pr->size);
	write(dect_fd, pr->img, pr->size);
}

static void send_start(uint8_t * in) {

  	uint8_t c[2];

	c[0] = 1;
}


static void set_baudrate(uint8_t * in) {

	uint8_t c[2];

	c[0] = PRELOADER_BAUD_230400;
	
	util_dump(c, 1, "[WRITE]");
	write(dect_fd, c, 1);

	tty_set_baud(dect_fd, B230400);
	
	c[0] = PRELOADER_NEW_BAUDRATE;
	util_dump(c, 1, "[WRITE]");
	write(dect_fd, c, 1);
}




void preloader_handler(void * stream, void * event) {

	uint8_t * in = (uint8_t *) event_data(event);
	
	switch (in[0]) {
	       
	case PRELOADER_READY:
		printf("PRELOADER_READY\n");
		set_baudrate(in);
		break;
		
	case PRELOADER_NEW_BAUDRATE_READY:
		printf("PRELOADER_NEW_BAUDRATE_READY\n");
		send_size(in);
		usleep(100*100);
		send_flashloader(in);
		break;

	default:
		if (in[0] == pr->checksum) {
			printf("Checksum ok!\n");
			
			/* make this prettier */
			/* state_add_handler(flashloader_state, dect_fd); */
			/* state_transition(FLASHLOADER_STATE); */
		} else {
			printf("Unknown preloader packet: %x\n", in[0]);
		}
		break;
	}

}



void preloader_init(void * stream) {
	
	uint8_t c = PRELOADER_START;
	
	printf("preloader_init\n");
	stream_add_handler(stream, preloader_handler);
	dect_fd = stream_get_fd(stream);

	read_flashloader();
	calculate_checksum();

	tty_set_baud(dect_fd, B9600);

	util_dump(&c, 1, "[WRITE]");
	write(dect_fd, &c, 1);
}


