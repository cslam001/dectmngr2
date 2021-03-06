#ifndef BUSMAIL_H
#define BUSMAIL_H

#include <stdint.h>
#include "buffer.h"

#define API_PROG_ID 0x00
#define API_TASK_ID 0x01

#define BUSMAIL_PACKET_HEADER 0x10
#define BUSMAIL_HEADER_SIZE 3
#define BUSMAIL_PACKET_OVER_HEAD 4

#define PACKET_SIZE 500

/* Information frame */
#define TX_SEQ_MASK ((1 << 6) | (1 << 5) | (1 << 4))
#define TX_SEQ_OFFSET 4
#define RX_SEQ_MASK  ((1 << 2) | (1 << 1) | (1 << 0))
#define RX_SEQ_OFFSET 0
#define PROG_ID_OFFSET 1
#define TASK_ID_OFFSET 2
#define PF_MASK ((1 << 3))
#define PF_OFFSET 3

#define PACKET_TYPE_MASK (1 << 7)
#define INFORMATION_FRAME (0 << 7)
#define CONTROL_FRAME (1 << 7)

#define CONTROL_FRAME_MASK ((1 << 7) | (1 << 6))
#define UNNUMBERED_CONTROL_FRAME ((1 << 7) | (1 << 6))
#define SUPERVISORY_CONTROL_FRAME ((1 << 7) | (0 << 6))


/* Supervisory control frame */
#define SUID_MASK ((1 << 5) | (1 << 4))
#define SUID_RR   ((0 << 1) | (0 << 0))
#define SUID_REJ  ((0 << 1) | (1 << 0))
#define SUID_RNR  ((1 << 1) | (0 << 0))
#define SUID_OFFSET 4
#define NO_PF 0
#define PF 1

#define POLL_FINAL (1 << 3)
#define SAMB_POLL_SET 0xc8
#define SAMB_NO_POLL_SET 0xc0


enum busmail_conf_t {
	MIN_PKT_DELAY,																// Minimum delay in usec between transmitted packets
};


typedef struct {
	int fd;
	uint32_t size;
	uint8_t data[PACKET_SIZE];
} packet_t;


typedef struct {
	uint8_t * data;
	int size;
	uint8_t task_id;
} tx_packet_t;


typedef struct __attribute__((__packed__)) 
{
	uint8_t frame_header;
	uint8_t program_id;
	uint8_t task_id;
	uint16_t mail_header;
	uint8_t mail_data[1];
} busmail_t;


typedef struct {
	uint32_t fd;
	uint8_t tx_seq_l;
	uint8_t rx_seq_l;
	uint8_t tx_seq_r;
	uint8_t rx_seq_r;
	void * tx_fifo;
	buffer_t * buf;
	void * application_handlers;
} busmail_connection_t;



struct mail_protocol_t {
	void* (*new)(int fd);
	void (*add_handler)(void *_self, void (*app_handler)(packet_t *));
	int (*dispatch)(void *_self);
	int (*send)(void *_self, uint8_t *data, int size);
	int (*receive)(void *_self, void *event);
	int (*conf)(void *_self, enum busmail_conf_t key, void *value);
};


extern struct mail_protocol_t mailProto;

void * busmail_new(int fd);
int busmail_conf(void *_self, enum busmail_conf_t key, void *value);
void busmail_add_handler(void * _self, void (*app_handler)(packet_t *));
int busmail_get(void * _self, packet_t *p);
void packet_dump(packet_t *p);
int busmail_dispatch(void * _self);
void busmail_ack(void * _self);
int busmail_receive(void * _self, void * event);
void busmail_send0(void * _self, uint8_t * data, int size);
int busmail_send(void * _self, uint8_t * data, int size);
void busmail_send_task(void * _self, uint8_t * data, int size, int task_id);
void busmail_send_addressee(void * _self, uint8_t * data, int size);

#endif /* BUSMAIL_H */
