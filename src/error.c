
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "ubus.h"

void exit_failure(const char *format, ...)
{
#define BUF_SIZE 500
	char err[BUF_SIZE], msg[BUF_SIZE];
	va_list ap;
	
	strncpy(err, strerror(errno), BUF_SIZE);

	va_start(ap, format);
	vsprintf(msg, format, ap);
	va_end(ap);
	
	fprintf(stderr, "%s: %s\n", msg, err);
	ubus_disable_receive();
	exit(EXIT_FAILURE);
}



void err_exit(const char *format, ...)
{
#define BUF_SIZE 500
	char msg[BUF_SIZE];
	va_list ap;
	
	va_start(ap, format);
	vsprintf(msg, format, ap);
	va_end(ap);
	
	fprintf(stderr, "%s\n", msg);
	ubus_disable_receive();
	exit(EXIT_FAILURE);
}


void exit_succes(const char *format, ...)
{
#define BUF_SIZE 500
	char msg[BUF_SIZE];
	va_list ap;
	
	va_start(ap, format);
	vsprintf(msg, format, ap);
	va_end(ap);
	
	printf("%s\n", msg);
	ubus_disable_receive();
	exit(EXIT_SUCCESS);
}

