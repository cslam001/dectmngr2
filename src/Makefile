

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





dectmngr2: boot.o tty.o error.o main.o state.o util.o preloader.o flashloader.o Crc.o app.o buffer.o busmail.o nvs.o fifo.o test.o list.o eap.o stream.o event.o
	$(CC) $(INCLUDE_PATHS) -o $@ $^


clean:
	rm -rf *.o
	rm -rf dectmngr2




