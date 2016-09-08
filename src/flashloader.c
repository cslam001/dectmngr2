
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
#include <sys/select.h>
#include <termios.h>


#include "tty.h"
#include "error.h"
#include "boot.h"
#include "util.h"
#include "buffer.h"
#include "preloader.h"
#include "busmail.h"
#include "stream.h"
#include "preloader.h"
#include "flashloader.h"
#include "Crc.h"
#include "event.h"

#include "MRtxDef.h"
#include "MailDef.h"


#define BUF_SIZE 5000
#define SECTOR_ERASE_CMD 0x30
#define CHIP_ERASE_CMD 0x10

#define TIMEOUT_10MS					10
#define TIMEOUT_100MS					(10 * TIMEOUT_10MS)
#define TIMEOUT_SEC						(10 * TIMEOUT_100MS)
#define ERASE_CONFIRM_TIMEOUT			(205 * TIMEOUT_SEC)
#define QUICK_CONFIRM_TIMEOUT			(7 * TIMEOUT_100MS)
#define RETRANSMIT_WAIT_TIMEOUT			(3 * TIMEOUT_10MS)


static int dect_fd;

typedef struct __attribute__((__packed__))
{
  PrimitiveType     Primitive;      /*!< Primitive */
  uint8_t           RequestID;      /*!< Request ID */
  uint8_t           MaxFreqSingle;  /*!< MaxFreqSingle */
  uint8_t           MaxFreqQuad;    /*!< MaxFreqQuad   */
  uint8_t           Manufacturer;   /*!< Manufacturer */
  uint16_t          DeviceId;       /*!< DeviceId      */
  uint32_t          TotalSize;      /*!< TotalSize     */
  uint32_t          SectorSize;     /*!< SectorSize    */
} flash_type_t;


typedef struct __attribute__((__packed__))
{
  PrimitiveType     Primitive;      /*!< Primitive */
  uint32_t          Address;        /*!< Memory address  */
  uint16_t           EraseCommand;   /*!< Erase command Chip/Sector/Block */    
} erase_flash_req_t;

typedef struct __attribute__((__packed__))
{
  PrimitiveType     Primitive;      /*!< Primitive */
  uint32_t            Address;        /*!< Memory address  */
  uint8_t          Confirm;        /*!< Confirm TRUE/FALSE */
} erase_flash_cfm_t;


typedef struct __attribute__((__packed__))
{
  PrimitiveType     Primitive;      /*!< Primitive */
  uint32_t            Address;        /*!< Memory address */
  uint16_t          Length;         /*!< Length of data */
  uint16_t          Data[1];        /*!< array of 16 bits data */
} prog_flash_t;

typedef struct __attribute__((__packed__))
{
  PrimitiveType     Primitive;      /*!< Primitive */
  uint32_t            Address;        /*!< Memory address  */
  uint16_t          Length;         /*!< Length of data */
  uint8_t          Confirm;        /*!< Confirm TRUE/FALSE */
} prog_flash_cfm_t;

typedef struct __attribute__((__packed__))
{
  PrimitiveType     Primitive;      /*!< Primitive */
  uint32_t              FirstProgWord;  /*!< First Program word */
  uint32_t              SecondProgWord; /*!< Second Program word  */
  uint32_t               OffsetAddress;  /*!< Data offset address  */
  FlashLoaderConfigType Config;         /*!< Configuration word */
} write_config_req_t;


typedef struct __attribute__((__packed__))
{
  PrimitiveType     Primitive;      /*!< Primitive */
  uint8_t             Confirm;        /*!< Confirm TRUE/FALSE */
} write_config_cfm_t;


typedef struct __attribute__((__packed__))
{
  PrimitiveType     Primitive;      /*!< Primitive */
  uint32_t            Address;        /*!< Memory address */
  uint32_t          Length;         /*!< Length of data */
} calc_crc32_req_t;

typedef struct __attribute__((__packed__))
{
  PrimitiveType     Primitive;      /*!< Primitive */
  uint32_t            Address;        /*!< Memory address */
  uint32_t          Length;         /*!< Length of data */
  uint32_t            Crc32;          /*!< Crc32 */
} calc_crc32_cfm_t;

static struct bin_img flashloader;
static struct bin_img *pr = &flashloader;

static flash_type_t flash;
static flash_type_t *f = &flash;  


int sectors, mod, sectors_written;

buffer_t * buf;



//-----------------------------------------------------------------------------
//   
//   Packet format
//   =============
//
//  +-----+----------------- 
//  | 0   | Header
//  +-----+----------------- 
//  | 1   | Length lsb          n=Length
//  +-----+----------------- 
//  | 2   | Length msb
//  +-----+----------------- 
//  | 3   | Payload 1
//  +-----+----------------- 
//  | .   | ...
//  +-----+----------------- 
//  | .   | ... 
//  +-----+----------------- 
//  | 2+n | Payload n
//  +-----+----------------- 
//  | 3+n | crc lsb
//  +-----+----------------- 
//  | 4+n | crc msb
//  +-----+----------------- 
//   
//   
//-----------------------------------------------------------------------------



static uint8_t * make_tx_packet(uint8_t * tx, void * packet, int data_size) {
  
  uint8_t * data = (uint8_t *) packet;
  int i;
  uint16_t crc = 0;
  
  tx[0] = UART_PACKET_HEADER;
  tx[1] = (uint8_t) data_size;
  tx[2] = (uint8_t) (data_size >> 8);
  
  for (i=0; i<data_size; i++) {
    crc = UpdateCrc(data[i], crc);
    tx[3 + i] = data[i];
  }
  
  tx[3 + data_size] = (uint8_t) crc;
  tx[4 + data_size] = (uint8_t) (crc >> 8);
  
  return tx;
}


// Send a packet to Dect and wait for an answer. If we
// don't get any response we retransmit same packet again.
static void send_packet_timeout(void *data, int size, int fd, int timeout) {
	int tx_size, res;
	struct timeval t;
	uint8_t *buf;
	fd_set rfds;

	tx_size = size + PACKET_OVER_HEAD;
	buf = malloc(tx_size);
	make_tx_packet(buf, data, size);
	t.tv_sec = timeout / 1000;
	t.tv_usec = (timeout - t.tv_sec * 1000) * 1000;

	do {
		if(tx_size < (int) MAX_PAYLOAD_SIZE / 4) {								// Debug print small packets
			util_dump(buf, tx_size, "[WRITE]");
		}

		while(write(fd, buf, tx_size) != tx_size) usleep(0);
		tty_drain(fd);

		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		res = select(fd + 1, &rfds, NULL, NULL, &t);							// Wait for data from Dect
		if(res == -1) {
			perror("Error waiting for packet ack");
		}
		else if(res == 0) {
			printf("Timeout, retransmitting\n");
			usleep(RETRANSMIT_WAIT_TIMEOUT * 1000);
		}
	} while(res == 0);

	free(buf);
}

static void calculate_checksum(void) {
	/* Calculate Checksum of flash loader */
	InitCrc32Table();
	pr->checksum = CalculateCRC32((uint16 *)pr->img, pr->size);

	printf("checksum: %x\n", pr->checksum);
}


static void sw_version_req(int fd) {
  
	SwVersionReqType *r = malloc(sizeof(SwVersionReqType));
  
	r->Primitive = READ_SW_VERSION_REQ;
	send_packet_timeout(r, sizeof(SwVersionReqType), fd, QUICK_CONFIRM_TIMEOUT);
	free(r);
}


static void sw_version_cfm(packet_t *p) {
  
	SwVersionCfmType *m = (SwVersionCfmType *) &p->data[0];

	printf("version: %d\n", m->Version);
	printf("revision: %d\n", m->Revision);
	printf("flashloader id: %d\n", m->FlashLoaderId);
	
}



static void qspi_flash_type_req(void) {

	ReadProprietaryDataReqType *r = malloc(sizeof(ReadProprietaryDataReqType));
  
	r->Primitive = READ_PROPRIETARY_DATA_REQ;
	r->RequestID = 0;

	send_packet_timeout(r, sizeof(ReadProprietaryDataReqType),
		dect_fd, QUICK_CONFIRM_TIMEOUT);
	free(r);

}


static void qspi_flash_type_cfm(packet_t *p) {

	memcpy(f, &(p->data[0]), sizeof(flash_type_t));

	printf("Primitive: %x\n", f->Primitive);
	printf("RequestId: %x\n", f->RequestID);
	printf("MaxFreqSingle: %x\n", f->MaxFreqSingle);
	printf("MaxFreqQuad: %x\n", f->MaxFreqQuad);
	printf("Manufacturer: %x\n", f->Manufacturer);
	printf("DeviceId: %x\n",  f->DeviceId);
	printf("TotalSize: %x\n", (int) f->TotalSize);
	printf("SectorSize: %x\n",(int) f->SectorSize);
	
}



static void config_target(void) {
	
	write_config_req_t *r = malloc(sizeof(write_config_req_t));
  
	r->Primitive = WRITE_CONFIG_REQ;
	r->FirstProgWord = 0;
	r->SecondProgWord = 0;
	r->OffsetAddress = 0xf0000;
	r->Config = QSPI_FLASH_CONFIG;

	send_packet_timeout(r, sizeof(write_config_req_t),
		dect_fd, QUICK_CONFIRM_TIMEOUT);
	free(r);
}


static void write_config_cfm(packet_t *p) {
	
	write_config_cfm_t *m = (write_config_cfm_t *) &p->data[0];
	
	if(m->Confirm == TRUE) {
		printf("Confirm: TRUE\n");
	}

}

static void read_firmware(void) {
	
	int fd;
	struct stat s;

	fd = open("/etc/dect/target.bin", O_RDONLY);
	if (fd == -1) {
		perror("open");
		exit(EXIT_FAILURE);
	}

	fstat(fd, &s);
	pr->size = s.st_size;
	
	/* From original FL 
	Add 3 dummy bytes in end of array in case of not byte access(8bit) but
	word access(xbit), and hex file exist of odd data bytes. Then
	set last undefined byte(s) to 0xFF
	Word access could be 16 bits access or 32bits access therefore 3 extra bytes. */
	pr->img = malloc(pr->size + 3);
	
	/* if Prog size is odd */
	if (pr->size & 1) {
		pr->img[pr->size] = 0xff;
		pr->img[pr->size + 1] = 0xff;
		pr->img[pr->size + 2] = 0xff;
	}

	if (read(fd, pr->img, pr->size) < pr->size) {
		perror("read");
		exit(EXIT_FAILURE);
	}
	

	printf("Size: 0x%x\n", (int)pr->size);
	printf("Size: %d\n", (int)pr->size);
	
	close(fd);
}





static void erase_flash_req(int address) {
	
	erase_flash_req_t *m = malloc(sizeof(erase_flash_req_t));
	
	m->Primitive = FLASH_ERASE_REQ;
	m->Address = address;
	m->EraseCommand = CHIP_ERASE_CMD;

	send_packet_timeout(m, sizeof(erase_flash_req_t),
		dect_fd, ERASE_CONFIRM_TIMEOUT);
	free(m);
}


static void flash_erase_cfm(packet_t *p) {
	
	erase_flash_cfm_t  *m = (erase_flash_cfm_t *) &p->data[0];
	printf("Address: 0x%x\n", m->Address);
	printf("Confirm: 0x%x\n", m->Confirm);
	
	  
	if(m->Confirm == TRUE) {
		/* sectors_written++; */

		/* if (sectors_written < sectors) { */
			
		/* 	/\* erase next sector *\/ */
		/* 	printf("."); */
		/* 	erase_flash_req(e, p->Address + f->SectorSize); */
		/* } else { */
			printf("flash_erased\n");
		/* } */
			

	} /* else { */
	/* 	/\* try again *\/ */
	/* 	printf("Flash erase failed. Retrying.\n"); */
	/* 	erase_flash_req(e, p->Address); */
	/* } */
}


static void erase_flash(void) {
	sectors = pr->size / f->SectorSize + 1;
	mod = pr->size % f->SectorSize;

	printf("image size: %d\n", pr->size);
	printf("f->SectorSize: %d\n", f->SectorSize);

	printf("sectors: %d\n", sectors);
	printf("mod: %d\n", mod);

	printf("Erasing flash\n");
	/* Erase first sector */
	sectors_written = 0;
	erase_flash_req(0);

}


static int is_data_ff(uint8_t *data) {	
	int i;

	for (i = 0; i < MAX_PAYLOAD_DATA_BYTES; i++) {
		if (data[i] != 0xff) {
			return 0;
		}
	}
	return 1;
}


static void prog_flash_req(int offset) {
	prog_flash_t * m = malloc(sizeof(prog_flash_t) + MAX_PAYLOAD_DATA_BYTES - 2);
	uint8_t *data = pr->img + offset;
	int of = offset;
	int data_size = MAX_PAYLOAD_DATA_BYTES;

	/* Skip data if all zeros */
	while (of < pr->size && is_data_ff(data)) {
		data += data_size;
		of += data_size;
	};

	/* Is the remaining data smaller than max packet size */
	if ((of + data_size) > pr->size) {
		data_size = (pr->size - of);
		
		/* If data size is odd */
		if (data_size & 1) {
			/* We added three bytes to the buffer
			 when we read the bin file */
			data_size++;
		}
		
		m->Length = data_size / 2;
		memcpy(m->Data, data, data_size);
	} else {
		m->Length = data_size / 2;
		memcpy(m->Data, data, data_size);
	}
	
	m->Primitive = PROG_FLASH_REQ;
	m->Address = of;
	
	/*printf("FLASH_PROG_REQ\n");
	printf("Address: 0x%x\n", m->Address);
	printf("Length: 0x%x\n", m->Length);*/
	
	send_packet_timeout(m, sizeof(prog_flash_t) + data_size - 2,
		dect_fd, QUICK_CONFIRM_TIMEOUT);
	free(m);
}


static void calc_crc32_req(void) {
	calc_crc32_req_t *m;
	uint32_t timeout;

	m = (calc_crc32_req_t*) malloc(sizeof(calc_crc32_req_t));
	m->Primitive = CALC_CRC32_REQ;
	m->Address = 0;
	m->Length = pr->size;

	// Confirm timeout 1 sec pr 1mbyte
	timeout = (m->Length / 0x10000) * TIMEOUT_SEC;
	timeout += 5 * TIMEOUT_SEC;
	
	printf("CALC_CRC32_REQ\n");
	printf("m->Address: %x\n", m->Address);
	printf("m->Length: %x\n", m->Length);

	send_packet_timeout(m, sizeof(calc_crc32_req_t), dect_fd, timeout);
	free(m);
}


static void calc_crc32_cfm(packet_t *p) {
	calc_crc32_cfm_t * m = (calc_crc32_cfm_t *) &p->data[0];

	printf("CALC_CRC32_CFM\n");	
	printf("m->Address: %x\n", m->Address);
	printf("m->Length: %x\n", m->Length);
	printf("m->Crc32: %x\n", m->Crc32);

	if (m->Crc32 == pr->checksum) {
		printf("Checksum (0x%x) ok!\n", m->Crc32);
		exit(0);
	} else {
		printf("Bad checksum! 0x%x != 0x%x\n", m->Crc32, pr->checksum);
		exit(-1);
	}
}


static void prog_flash_cfm(packet_t *p) {	
	prog_flash_cfm_t * m = (prog_flash_cfm_t *) &p->data[0];

	/*printf("FLASH_PROG_CFM\n");
	printf("Address: 0x%x\n", m->Address);
	printf("Length: 0x%x\n", m->Length);
	printf("Confirm: 0x%x\n", m->Confirm);*/

	
	if (m->Confirm == TRUE) {
		printf(".");
		
		if ((m->Address + 0x800) < (uint32_t) pr->size) {
			prog_flash_req(m->Address + 0x800);
		} else {
			printf("\ndone programming\n");
			calc_crc32_req();
		}

	} else {
		printf("FALSE\n");
		prog_flash_req(m->Address + 0);
	}	
}



void flashloader_dispatch(packet_t *p) {

  	//packet_dump(p);

	switch (p->data[0]) {

	case READ_SW_VERSION_CFM:
		printf("READ_SW_VERSION_CFM\n");
		sw_version_cfm(p);
		printf("WRITE_CONFIG_REQ\n");
		config_target();
		break;
		
	case WRITE_CONFIG_CFM:
		printf("WRITE_CONFIG_CFM\n");
		write_config_cfm(p);
		printf("READ_PROPRIETARY_DATA_REQ\n");
		qspi_flash_type_req();
		break;

	case READ_PROPRIETARY_DATA_CFM:
		printf("READ_PROPRIETARY_DATA_CFM\n");
		qspi_flash_type_cfm(p);
		
		/* Erase flash */
		printf("FLASH_ERASE_REQ\n");
		erase_flash();

		break;

	case FLASH_ERASE_CFM:
		printf("FLASH_ERASE_CFM\n");
		flash_erase_cfm(p);

		/* Progam flash */
		printf("program flash\n");
		prog_flash_req(0);
		break;

	case PROG_FLASH_CFM:
		prog_flash_cfm(p);
		break;

	case CALC_CRC32_CFM:
		calc_crc32_cfm(p);
		break;


	default:
		printf("Unknown flashloader packet: %x\n", p->data[0]);
		break;
	}
}



int flashpacket_get(packet_t *p, buffer_t *b) {
	uint32_t i, size, read = 0;;
	uint16_t crc = 0, crc_calc = 0;
	uint8_t buf[MAX_MAIL_BUFFER_SIZE];

	/* Do we have a start of frame? */	
	while (buffer_read(b, buf, 1) > 0) {
		read++;
		if (buf[0] == UART_PACKET_HEADER) {
			break;
		}
	}

	/* Return if we did not read any data */
	if (read == 0) {
		return -1;
	}

	/* Do we have a full header? */
	if (buffer_size(b) < 2) {
		buffer_rewind(b, 1);
		return -1;
	}
	buffer_read(b, buf + 1, 2);

	/* Packet size */
	size = (((int) buf[2] << 8) | (int) buf[1]);

	/* Do we have a full packet? */
	if (buffer_size(b) < size + 2) {
		buffer_rewind(b, 3);
		return -1;
	}
	buffer_read(b, buf + 3, size + 2);
	
	/* Read packet checksum */
	crc = (((uint16_t) buf[PACKET_OVER_HEAD + size - 1]) << 8) | 
		((uint16_t) buf[PACKET_OVER_HEAD + size - 2]);

	/* Calculate checksum over data portion */
	for (i = 0; i < size; i++) {
		crc_calc = UpdateCrc(buf[i + 3], crc_calc);
	}

	if (crc != crc_calc) {
		printf("Drop packet: bad packet checksum: %x != %x\n", crc, crc_calc);
		return -1;
	}

	/* Copy data portion to packet */
	memcpy(p->data, buf + 3, size);
	p->size = size;

	return 0;
}



void flashloader_handler(void *stream __attribute__((unused)), void * event) {
	packet_t packet;
	packet_t *p = &packet;

	//util_dump(e->in, e->incount, "\n[READ]");

	/* Add input to buffer */
	if (buffer_write(buf, event_data(event), event_count(event)) == 0) {
		printf("buffer full\n");
	}
	
	/* Process whole packets in buffer */
	while(flashpacket_get(p, buf) == 0) {
		flashloader_dispatch(p);
	}
}


void flashloader_init(void * stream) {
	printf("flashloader_init\n");

	dect_fd = stream_get_fd(stream);
	stream_add_handler(stream, MAX_EVENT_SIZE, flashloader_handler);

	usleep(300*1000);
	sw_version_req(dect_fd);
	read_firmware();
	calculate_checksum();

	buf = buffer_new(5000);
}






