

ifeq ($(NATALIE_DIR),)
INCLUDE_PATHS += -I$(STAGING_DIR)/usr/include/natalie-dect/ \
		-I$(STAGING_DIR)/usr/include/natalie-dect/Phoenix
INCLUDE_PATHS += -I$(STAGING_DIR)/usr/include/bcm963xx/shared/opensource/include/bcm963xx/
INCLUDE_PATHS += -I$(STAGING_DIR)/usr/include/bcm963xx/bcmdrivers/opensource/include/bcm963xx/

else
INCLUDE_PATHS += -I$(NATALIE_DIR)/SrcHeaders \
	-I$(NATALIE_DIR)/SrcHeaders/Phoenix
INCLUDE_PATHS += -I$(BCM_KERNEL_DIR)/bcm963xx/shared/opensource/include/bcm963xx/
INCLUDE_PATHS += -I$(BCM_KERNEL_DIR)/cm963xx/bcmdrivers/opensource/include/bcm963xx/
endif

%.o: %.c
	$(CC) -c $(CFLAGS) $(INCLUDE_PATHS) -o $@ $<





dectmngr2: tty.o error.o main.o state.o util.o Crc.o app.o buffer.o busmail.o nvs.o fifo.o list.o eap.o stream.o event.o event_base.o internal_call.o connection_init.o api_parser.o
	$(CC) $(INCLUDE_PATHS) -o $@ $^


clean:
	rm -rf *.o
	rm -rf dectmngr2




