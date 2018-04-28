#pragma once

#include "ntddk.h"

#define _REF_ // marked input&output param in method 




typedef struct RingBuffer* PRINGBUFFER;

INT RBInit(IN PRINGBUFFER* pRingBuf, IN ULONG Size);
INT RBDeinit(IN PRINGBUFFER pRingBuf);
ULONG RBWrite(_REF_ PRINGBUFFER pRingBuf, IN PCHAR pBuf, IN ULONG Size);
INT RBRead(_REF_ PRINGBUFFER pRingBuf, OUT PCHAR pBuf,  PULONG pSize);
ULONG RBSize(PCHAR Head, PCHAR Tail, ULONG Capacity);
INT RBLoadFactor(PRINGBUFFER pRingBuf);