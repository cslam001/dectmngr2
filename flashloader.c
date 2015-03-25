
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
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


#define BUF_SIZE 500

static struct bin_img flashloader;
static struct bin_img *pr = &flashloader;

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


static void send_size(event_t *e) {

	uint8_t c[2];

	/* Reply */
	c[0] = pr->size_msb;
	c[1] = pr->size_lsb;

	util_dump(c, 2, "[WRITE]");
	write(e->fd, c, 2);
}


static void send_flashloader(event_t *e) {
  
	/* memcpy(e->out, pr->img, pr->size); */
	/* e->outcount = pr->size; */

	util_dump(pr->img, pr->size, "[WRITE]");
	write(e->fd, pr->img, pr->size);

}

static void send_start(event_t *e) {
  
	e->out[0] = 1;
	e->outcount = 1;
}




void init_flashloader_state(int dect_fd) {
	
	printf("FLASHLOADER_STATE\n");

	/* read_firmware(); */
	/* calculate_checksum(); */

}


void handle_flashloader_package(event_t *e) {

	
	switch (e->in[0]) {
	       
	default:
		printf("Unknown flashloader packet: %x\n", e->in[0]);
		break;
	}

}





struct state_handler flashloader_handler = {
	.state = FLASHLOADER_STATE,
	.init_state = init_flashloader_state,
	.event_handler = handle_flashloader_package,
};

struct state_handler * flashloader_state = &flashloader_handler;
