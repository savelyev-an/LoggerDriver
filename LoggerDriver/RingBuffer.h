#pragma once

#include "ntddk.h"

#define _REF_ // marked input&output param in method 
/*
* RingBuffer is a part of the KLogger
*/
typedef struct RingBuffer* PRINGBUFFER;

INT RBInit(IN PRINGBUFFER* pRingBuf, IN ULONG Size);
INT RBDeinit(IN PRINGBUFFER pRingBuf);
ULONG RBWrite(_REF_ PRINGBUFFER pRingBuf, IN PCHAR pBuf, _REF_ PULONG pCharsToWrite);
INT RBRead(_REF_ PRINGBUFFER pRingBuf, OUT PCHAR pBuf, _REF_ PULONG pSize);
ULONG RBSize(IN PCHAR Head, IN PCHAR Tail, IN ULONG Capacity);
INT RBLoadFactor(IN PRINGBUFFER pRingBuf);