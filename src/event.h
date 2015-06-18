#ifndef EVENT_H
#define EVENT_H

void * event_new(int fd);
void event_destroy(void * event);
uint8_t * event_data(void * event);
int event_count(void * event);


#endif /* EVENT_H */



