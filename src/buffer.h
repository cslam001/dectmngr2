
#ifndef BUFFER_H
#define BUFFER_H

#include <stdint.h>


typedef struct {
	uint8_t * buf_start;			// malloc begin
	uint8_t * buf_end;				// malloc end
	uint8_t * data_start;			// read pointer
	uint8_t * data_end;				// write pointer
	uint32_t count;					// used length
	uint32_t buf_size;				// sizeof malloc
	uint8_t state;
} buffer_t;

buffer_t * buffer_new(int size);
int buffer_dump(buffer_t * self);
uint32_t buffer_write(buffer_t * self, uint8_t *input, uint32_t count);
uint32_t buffer_read(buffer_t * self, uint8_t *buf, uint32_t count);
uint32_t buffer_rewind(buffer_t * self, uint32_t count);
uint32_t buffer_size(buffer_t * self);

#endif /* BUFFER_H */
