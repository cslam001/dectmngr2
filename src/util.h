#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>
#include <Api/RsStandard.h>

struct bin_img {
	uint8_t *img;
	int size;
	uint8_t size_msb;
	uint8_t size_lsb;
	uint32_t checksum;
};

typedef struct {
	uint32_t mode;
	uint8_t test_enable;
} config_t;


void util_dump(unsigned char *buf, int size, char *start);
void util_write(void *data, int size, int fd);
int check_args(int argc, char * argv[], config_t * c);
int initial_transition(config_t * config, int dect_fd);
int gpio_control(int gpio, int state);
int dect_chip_reset(void);
void print_status(RsStatusType s);

#endif /* UTIL_H */
