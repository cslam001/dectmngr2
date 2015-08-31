

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

CFLAGS += -Wall -Werror=implicit-function-declaration

%.o: %.c
	$(CC) -c $(CFLAGS) $(INCLUDE_PATHS) -o $@ $<


# Ability to build without ubus etc. for Sercomm
CFLAGS += -DWITH_UBUS
LIBS += -lubox -lubus -lblobmsg_json -ljson-c
dectmngr2: ubus.o


dectmngr2: prog.o boot.o preloader.o flashloader.o tty.o
dectmngr2: error.o main.o state.o util.o Crc.o app.o buffer.o
dectmngr2: busmail.o nvs.o fifo.o list.o eap.o stream.o event.o
dectmngr2: event_base.o internal_call.o connection_init.o api_parser.o
dectmngr2: handset.o
	$(CC) $(CFLAGS) $(INCLUDE_PATHS) -o $@ $^ $(LIBS)


clean:
	rm -rf *.o
	rm -rf dectmngr2




