
#ifndef HANDSET_H
#define HANDSET_H


//-------------------------------------------------------------
#define MAX_NR_HANDSETS 20

struct terminal_t {
	uint8_t pinging;
	uint32_t id;
	uint8_t ipui[5];
};


struct handsets_t {
	int termCount;											// Number of terminals registered
	struct terminal_t terminal[MAX_NR_HANDSETS];
};



//-------------------------------------------------------------
int list_handsets(void);
int delete_handset(int id);
void handset_init(void *bus);


#endif
