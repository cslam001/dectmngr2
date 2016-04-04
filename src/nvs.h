
#ifndef NVS_H
#define NVS_H

#include <stdint.h>
#include "util.h"

#define DECT_NVS_SIZE 4096


void nvs_init(void * event_base, config_t * config);
int nvs_file_write(uint32_t offset, uint32_t len, uint8_t *data);
int nvs_file_read(uint32_t *len, uint8_t *data);
int nvs_rfpi_patch(uint8_t *data);
int nvs_freq_patch(uint8_t *data);
int nvs_emc_patch(uint8_t *data);


#endif

