
#ifndef LIST_H
#define LIST_H

void * list_new(void);
void list_add(void * _self, int fd);
void list_delete(void * _self, int fd);
void list_call_each(void * _self, void * p);

#endif /* LIST_H */


