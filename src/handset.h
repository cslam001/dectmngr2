
#ifndef HANDSET_H
#define HANDSET_H

#include <Api/RsStandard.h>
#include <Api/Types/ApiTypes.h>


//-------------------------------------------------------------
#define MAX_NR_HANDSETS 20

struct terminal_t {
	uint32_t id;
	uint8_t ipui[5];
	ApiCcBasicServiceType BasicService;						// supports wideband
	ApiCodecListType *codecs;								// supported codecs
};


struct handsets_t {
	int termCount;											// Number of terminals registered (read from stack)
	int termCntExpt;										// Number of terminals we have calculated should exist
	int termCntEvntAdd;										// Delayed event indicator of what happened when terminal count changed
	int termCntEvntDel;										// Delayed event indicator of what happened when terminal count changed
	struct terminal_t terminal[MAX_NR_HANDSETS];
};




//-------------------------------------------------------------
struct handsets_t handsets;


//-------------------------------------------------------------
int list_handsets(void);
int delete_handset(int id);
int page_all_handsets(void);
void handset_init(void *bus);


#endif
