
#ifndef LIST_H
#define LIST_H

void * list_new(void);
void list_add(void * _self, void * p);
void list_delete(void * _self, void * p);
void list_call_each(void * _self, void * arg);

#endif /* LIST_H */


