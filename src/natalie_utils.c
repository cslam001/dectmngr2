#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>


#include <Api/FpGeneral/ApiFpGeneral.h>
#include <Api/CodecList/ApiCodecList.h>
#include <Api/FpCc/ApiFpCc.h>
#include <Api/FpMm/ApiFpMm.h>
#include <Api/ProdTest/ApiProdTest.h>
#include <Api/RsStandard.h>


#include "natalie_utils.h"

ApiInfoElementType *
ApiGetInfoElement(ApiInfoElementType *IeBlockPtr,
                                      rsuint16 IeBlockLength,
                                      ApiIeType Ie)
{
	/* Ie is in little endian inside the infoElement
	   list while all arguments to function are in bigEndian */
	ApiInfoElementType *pIe = NULL;
	rsuint16 targetIe = Ie;  

	while (NULL != (pIe = ApiGetNextInfoElement(IeBlockPtr, IeBlockLength, pIe))) {
		if (pIe->Ie == targetIe) {
			/* Return the pointer to the info element found */
			return pIe; 
		}
	}

	/* Return NULL to indicate that we did not
	   find an info element wirh the IE specified */
	return NULL; 
}


ApiInfoElementType* ApiGetNextInfoElement(ApiInfoElementType *IeBlockPtr,
                                          rsuint16 IeBlockLength,
                                          ApiInfoElementType *IePtr)
{
	ApiInfoElementType *pEnd = (ApiInfoElementType*)((rsuint8*)IeBlockPtr + IeBlockLength);

	if (IePtr == NULL) {
		// return the first info element
		IePtr = IeBlockPtr;
		
	} else {
		// calc the address of the next info element
		IePtr = (ApiInfoElementType*)((rsuint8*)IePtr + RSOFFSETOF(ApiInfoElementType, IeData) + IePtr->IeLength);
	}

	if (IePtr < pEnd) {
		
		return IePtr; // return the pointer to the next info element
	}
	return NULL; // return NULL to indicate that we have reached the end
}


void ApiBuildInfoElement(ApiInfoElementType **IeBlockPtr,
                         rsuint16 *IeBlockLengthPtr,
                         ApiIeType Ie,
                         rsuint8 IeLength,
                         rsuint8 *IeData)
{

	rsuint16 newLength = *IeBlockLengthPtr + RSOFFSETOF(ApiInfoElementType, IeData) + IeLength;

	/* Ie is in little endian inside the infoElement list while all arguments to function are in bigEndian */
	rsuint16 targetIe = Ie;
	//  RevertByteOrder( sizeof(ApiIeType),(rsuint8*)&targetIe   );          

	/* Allocate / reallocate a heap block to store (append) the info elemte in. */
	ApiInfoElementType *p = malloc(newLength);

	if (p == NULL) {

		// We failed to get e new block.
		// We free the old and return with *IeBlockPtr == NULL.
		ApiFreeInfoElement(IeBlockPtr);
		*IeBlockLengthPtr = 0;
	} else {
		// Update *IeBlockPointer with the address of the new block
		//     *IeBlockPtr = p;
		if( *IeBlockPtr != NULL ) {
		
			/* Copy over existing block data */
			memcpy( (rsuint8*)p, (rsuint8*)*IeBlockPtr, *IeBlockLengthPtr);
		
			/* Free existing block memory */
			ApiFreeInfoElement(IeBlockPtr);
		}
    
		/* Assign newly allocated block to old pointer */
		*IeBlockPtr = p;

		// Append the new info element to the allocated block
		p = (ApiInfoElementType*)(((rsuint8*)p) + *IeBlockLengthPtr); // p now points to the first byte of the new info element

		p->Ie = targetIe;

		p->IeLength = IeLength;
		memcpy (p->IeData, IeData, IeLength);
		// Update *IeBlockLengthPtr with the new block length
		*IeBlockLengthPtr = newLength;
	}

}




void ApiFreeInfoElement(ApiInfoElementType **IeBlockPtr)
{
	free((void*)*IeBlockPtr);

	*IeBlockPtr = NULL;
}
