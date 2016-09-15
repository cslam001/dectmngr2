

ifeq ($(NATALIE_DIR),)
INCLUDE_PATHS += -I$(STAGING_DIR)/usr/include/natalie-dect/
INCLUDE_PATHS += -I$(STAGING_DIR)/usr/include/natalie-dect/Phoenix
INCLUDE_PATHS += -I$(STAGING_DIR)/usr/include/bcm963xx/shared/opensource/include/bcm963xx/
INCLUDE_PATHS += -I$(STAGING_DIR)/usr/include/bcm963xx/bcmdrivers/opensource/include/bcm963xx/
else
INCLUDE_PATHS += -I$(NATALIE_DIR)/SrcHeaders
INCLUDE_PATHS += -I$(NATALIE_DIR)/SrcHeaders/Phoenix
INCLUDE_PATHS += -I$(BCM_KERNEL_DIR)/bcm963xx/shared/opensource/include/bcm963xx/
INCLUDE_PATHS += -I$(BCM_KERNEL_DIR)/bcm963xx/bcmdrivers/opensource/include/bcm963xx/
endif


# Dectmngr2 misbehaves on MIPS and sometimes crashes
# due to bad compiling. We try various compiler flags
# to force changed binary output. Unsure of what is
# the actuall problem...  /Ronny
CFLAGS += -O2 -fcaller-saves -fsection-anchors
CFLAGS += -Wall -Werror=implicit-function-declaration
CFLAGS += -Wextra -std=gnu99
# For debug during development
#CFLAGS += -g -fstack-protector -L $(STAGING_DIR)/target-*/root-brcm*/usr/lib/

%.o: %.c
	$(CC) -c $(CFLAGS) $(INCLUDE_PATHS) -o $@ $<


# Ability to build without ubus etc. for Sercomm
CFLAGS += -DWITH_UBUS
LIBS += -lubox -lubus
dectmngr2: ubus.o


dectmngr2: prog.o boot.o preloader.o flashloader.o tty.o
dectmngr2: error.o main.o util.o Crc.o app.o buffer.o natalie_utils.o
dectmngr2: busmail.o nvs.o fifo.o list.o eap.o stream.o event.o
dectmngr2: event_base.o external_call.o connection_init.o api_parser.o
dectmngr2: handset.o rawmail.o debugger.o rawmailproxy.o
	$(CC) $(CFLAGS) $(INCLUDE_PATHS) -o $@ $^ $(LIBS)


clean:
	rm -rf *.o
	rm -rf dectmngr2




