
#ifndef CONNECTION_INIT_H
#define CONNECTION_INIT_H


int connection_get_status(char **keys, char **values);
void connection_init(void * bus);
int connection_set_radio(int onff);
int connection_set_registration(int onoff);

#endif
