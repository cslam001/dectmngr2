


ApiInfoElementType *
ApiGetInfoElement(ApiInfoElementType *IeBlockPtr,
		  rsuint16 IeBlockLength,
		  ApiIeType Ie);


ApiInfoElementType * 
ApiGetNextInfoElement(ApiInfoElementType *IeBlockPtr,
                                          rsuint16 IeBlockLength,
                                          ApiInfoElementType *IePtr);


void ApiBuildInfoElement(ApiInfoElementType **IeBlockPtr,
                         rsuint16 *IeBlockLengthPtr,
                         ApiIeType Ie,
                         rsuint8 IeLength,
                         rsuint8 *IeData);


#ifndef RSOFFSETOF
/*! \def RSOFFSETOF(type, field)                                                                                                                        
 * Computes the byte offset of \a field from the beginning of \a type. */
#define RSOFFSETOF(type, field) ((size_t)(&((type*)0)->field))
#endif
