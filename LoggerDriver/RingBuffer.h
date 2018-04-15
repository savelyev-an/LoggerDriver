#pragma once

#include "ntddk.h"

typedef struct RingBuffer {
	PCHAR Data;
	PCHAR Head;
	PCHAR Tail;

	ULONGLONG Capacity;

	KSPIN_LOCK SplockTail;
	KSPIN_LOCK SplockHead;
	KSPIN_LOCK SplockWrite;

} RINGBUFFER;

typedef struct RingBuffer* PRINGBUFFER;

INT RBInit(PRINGBUFFER* pRingBuf, SIZE_T Size);
INT RBDeinit(PRINGBUFFER pRingBuf);
INT RBWrite(PRINGBUFFER pRingBuf, PCHAR pBuf, SIZE_T Size);
INT RBRead(PRINGBUFFER pRingBuf, PCHAR pBuf, PSIZE_T pSize);
SIZE_T RBSize(PCHAR Head, PCHAR Tail, SIZE_T Capacity);
INT RBLoadFactor(PRINGBUFFER pRingBuf);