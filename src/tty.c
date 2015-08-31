#include <termios.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>

#include "error.h"


int tty_set_raw(int fd)
{
	struct termios t;
	
	if (tcgetattr(fd, &t) == -1)
		return -1;

	t.c_lflag &= ~(ICANON | ISIG | IEXTEN | ECHO);

	t.c_iflag &= ~(BRKINT | ICRNL | IGNBRK | IGNCR | INLCR |
		       INPCK | ISTRIP | IXON | PARMRK);

	t.c_oflag &= ~OPOST;

	t.c_cc[VMIN] = 1;
	t.c_cc[VTIME] = 0;

	if (tcsetattr(fd, TCSAFLUSH, &t) == -1)
		return -1;

	return 0;
}

int tty_set_baud(int fd, int baud)
{
	struct termios tp;

	printf("SET_BAUD\n");

	usleep(300*1000);

	if (tcgetattr(fd, &tp) == -1)
		exit_failure("tcgetattr");
	
	if (cfsetospeed(&tp, baud) == -1)
		exit_failure("cfsetospeed");

	if (tcsetattr(fd, TCSAFLUSH, &tp) == -1)
		exit_failure("tcsetattr");

	usleep(300*1000);

	return 0;
}


int tty_open(const char * s) {

	int fd = open("/dev/ttyS1", O_RDWR);

	if (fd == -1) {
		exit_failure("open\n");
	}

	return fd;
}
