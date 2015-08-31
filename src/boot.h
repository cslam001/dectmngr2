
#ifndef BOOT_H
#define BOOT_H


extern struct state_handler * boot_state;

void boot_init(void *stream);
void boot_exit(void *stream);

#endif