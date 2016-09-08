#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buffer.h"
#include "error.h"


enum {
	NORMAL_STATE,
	WRAPPED_STATE,
};

buffer_t * buffer_new(int size) {

	buffer_t *b = (buffer_t *) calloc(1, sizeof(buffer_t));

	if (!b) {
		exit_failure("malloc\n");
	}
	
	b->buf_start = malloc(size);
	if (!b->buf_start) {
		exit_failure("malloc\n");
	}
	
	b->count = 0;
	b->buf_end = b->buf_start + size;
	b->buf_size = size;
	b->data_start = b->buf_start;
	b->data_end = b->buf_start;
	b->state = NORMAL_STATE;
	
	return b;
}


static uint32_t write_normal(buffer_t * self, uint8_t *input, uint32_t count) {

	/* Don't write beyond buffer boundary */
	if ( self->data_end + count > self->buf_end ) {
		count = self->buf_end - self->data_end;
	}

	memcpy(self->data_end, input, count);
	self->data_end += count;
	self->count += count;

	return count;
}


static uint32_t write_wrapped(buffer_t * self, uint8_t *input, uint32_t count) {

	/* Don't write beyond start of data */
	if ( self->data_end + count > self->data_start ) {
		count = self->data_start - self->data_end;
	}
		
	memcpy(self->data_end, input, count);
	self->data_end += count;
	self->count += count;

	return count;
}


static uint32_t read_normal(buffer_t * self, uint8_t *buf, uint32_t count) {

	/* Don't read beyond end of data */
	if ( count > self->count) {
		count = self->count;
	}

	memcpy(buf, self->data_start, count);
	self->data_start += count;
	self->count -= count;
	
	return count;
}

static uint32_t read_wrapped(buffer_t * self, uint8_t *buf, uint32_t count) {
	
	/* Don't read beyond end of buffer */
	if ( self->data_start + count > self->buf_end ) {
		count = self->buf_end - self->data_start;
	}
	/* Don't read beyond end of data */
	if ( count > self->count) {
		count = self->count;
	}

	memcpy(buf, self->data_start, count);
	self->data_start += count;
	self->count -= count;

	return count;
}


static uint32_t rewind_normal(buffer_t * self, uint32_t count) {

	/* Don't rewind beyond start of buffer */
	if ( self->data_start - count < self->buf_start ) {
		count = self->data_start - self->buf_start;
	} 
	
	self->data_start -= count;
	self->count += count;

	return count;
}


static uint32_t rewind_wrapped(buffer_t * self, uint32_t count) {

	/* Don't rewind beyond end of data */
	if ( self->data_start - count < self->data_end ) {
		count = self->data_start - self->data_end;
	}
	
	self->data_start -= count;
	self->count += count;

	return count;
}


uint32_t buffer_write(buffer_t * self, uint8_t *input, uint32_t count) {
	
	uint32_t written = 0;

	if (self->count == self->buf_size) {
		return 0;
	}

	if ( self->state == NORMAL_STATE) {

		written = write_normal(self, input, count);

		if ( written < count && self->count < self->buf_size ) {

			/* Wrap the buffer */
			self->state = WRAPPED_STATE;
			self->data_end = self->buf_start;

			written += write_wrapped(self, input + written, count - written);
		}
	
	} else if (self->state == WRAPPED_STATE )  {

		written = write_wrapped(self, input, count);

		if ( written < count && self->count < self->buf_size ) {

			/* Wrap the buffer */
			self->state = NORMAL_STATE;
			self->data_end = self->buf_start;
			
			written += write_normal(self, input + written, count - written);
		}
	}

	//buffer_dump(self);
	return written;
}


uint32_t buffer_read(buffer_t * self, uint8_t *buf, uint32_t count) {

	uint32_t read = 0;

	if ( self->state == NORMAL_STATE ) {

		read = read_normal(self, buf, count);
		
		if ( read < count && self->count > 0) {

			/* Wrap the buffer */
			self->state = WRAPPED_STATE;
			self->data_start = self->buf_start;
			
			read += read_wrapped(self, buf + read, count - read);
		}



	} else if (self->state == WRAPPED_STATE )  {

		read = read_wrapped(self, buf, count);
		
		if ( read < count && self->count > 0) {

			/* Wrap the buffer */
			self->state = NORMAL_STATE;
			self->data_start = self->buf_start;
			
			read += read_normal(self, buf + read, count - read);
		}

	} 

	//buffer_dump(self);
	return read;
}


uint32_t buffer_rewind(buffer_t * self, uint32_t count) {

	uint32_t rewinded = 0;

	if ( self->state == NORMAL_STATE ) {
		
		rewinded = rewind_normal(self, count);

		if ( rewinded < count ) {

			/* Wrap the buffer */
			self->state = WRAPPED_STATE;
			self->data_start = self->buf_end;

			rewinded += rewind_wrapped(self, count - rewinded);
		}

	} else if (self->state == WRAPPED_STATE )  {		

		rewinded = rewind_wrapped(self, count);
		
		if ( rewinded < count ) {

			/* Wrap the buffer */
			self->state = NORMAL_STATE;
			self->data_start = self->buf_end;

			rewinded += rewind_normal(self, count - rewinded);
		}
	}

	//buffer_dump(self);
	return rewinded;
}



int buffer_dump(buffer_t * self) {
	
	uint32_t i, data_start, data_end;
	
	data_start = self->data_start - self->buf_start;
	data_end = self->data_end - self->buf_start;

	printf("[BUFFER: count %d\t size %d] \n", self->count, self->buf_size);
	printf("[data_start %d\t data_end %d] \n", data_start, data_end);

	for (i = 0; i < self->buf_size; i++) {

		if ( i % 25 == 0 ) {
			printf("\n");
		}

		if ( i == data_start ) {
			printf("[");
		}

		if ( i == data_end ) {
			printf("]");
		}
		

		printf("%02x ", self->buf_start[i]);
	}
	printf("\n\n");

	return 0;
}


uint32_t buffer_size(buffer_t * self) {

	return self->count;
}
