#include "RingBuffer.h"

#include <winerror.h>


typedef struct RingBuffer {
	PCHAR pData; // the pointer to the 
	PCHAR volatile pHead; // the pointer to the next free element
	PCHAR volatile pTail; // the pointer to the first written element

	ULONG Capacity; // the length of the buffer

	KSPIN_LOCK SplockReadWrite; // only one Thread can write at the moment

} RINGBUFFER;

INT
RBInit( 
	IN PRINGBUFFER* pRingBuf,
	IN ULONG Size
) {
	INT Err = ERROR_SUCCESS;

	if (!pRingBuf) {
		Err = ERROR_BAD_ARGUMENTS;
		goto err_ret;
	}

	PRINGBUFFER RingBuf = (PRINGBUFFER)ExAllocatePool(NonPagedPool, sizeof(RINGBUFFER));
	if (!RingBuf) {
		Err = ERROR_NOT_ENOUGH_MEMORY;
		goto err_ret;
	}

	*pRingBuf = RingBuf;

	RingBuf->pData = (PCHAR)ExAllocatePool(NonPagedPool, Size * sizeof(CHAR));
	if (!RingBuf->pData) {
		Err = ERROR_NOT_ENOUGH_MEMORY;
		ExFreePool(RingBuf);
		goto err_ret;
	}

	RingBuf->pHead = RingBuf->pData;
	RingBuf->pTail = RingBuf->pData;
	RingBuf->Capacity = Size;

	KeInitializeSpinLock(&(RingBuf->SplockReadWrite));

err_ret:
	return Err;
}

INT
RBDeinit(
	IN PRINGBUFFER pRingBuf
) {
	if (!pRingBuf) {
		return ERROR_BAD_ARGUMENTS;
	}

	ExFreePool(pRingBuf->pData);
	ExFreePool(pRingBuf);

	return ERROR_SUCCESS;
}

ULONG
RBSize(
	IN PCHAR Head,
	IN PCHAR Tail,
	IN ULONG Capacity
) {
	if (Head >= Tail) {
		return (ULONG)(Head - Tail);

	} else {
		return (ULONG)(Capacity - (Tail - Head));
	}
}


static ULONG
RBFreeSize(
	IN PCHAR Head,
	IN PCHAR Tail,
	IN ULONG Capacity

) {
	return Capacity - RBSize(Head, Tail, Capacity)-1; // minus 1 to be sure that if Head=Tail => RingBuffer is empty
}

/*
* Fuction return amount of unwritten chars
*/
ULONG 
RBWrite(
	_REF_ PRINGBUFFER pRingBuf, 
	IN PCHAR pBuf, 
	_REF_ PULONG pCharsToWrite
) {
	int Size = *pCharsToWrite;
	int Err = ERROR_SUCCESS;

	if (!pRingBuf) {
		Err= ERROR_BAD_ARGUMENTS;
		goto out;
	}

	KIRQL OldIrql;
	KeRaiseIrql(HIGH_LEVEL, &OldIrql);
	KeAcquireSpinLockAtDpcLevel(&(pRingBuf->SplockReadWrite));

	PCHAR Head = pRingBuf->pHead;
	PCHAR Tail = pRingBuf->pTail;

	INT FreeBuf = RBFreeSize(Head, Tail, pRingBuf->Capacity);
	if (Size > FreeBuf){
		Size = FreeBuf;
		*pCharsToWrite = *pCharsToWrite - Size;
	} else {
		*pCharsToWrite = 0;
	}

	PCHAR NewHead;
	
	if (Head >= Tail) {
		ULONG DistToFinish = pRingBuf->Capacity - (Head - pRingBuf->pData);
		if (Size > DistToFinish) {
			RtlCopyMemory(Head, pBuf, DistToFinish);
			RtlCopyMemory(pRingBuf->pData, pBuf + DistToFinish, Size - DistToFinish);
			NewHead = pRingBuf->pData + Size - DistToFinish;
		} else {
			RtlCopyMemory(Head, pBuf, Size);
			NewHead = Head + Size;
		}

	} else {
		RtlCopyMemory(Head, pBuf, Size);
		NewHead = Head + Size;
	}
	pRingBuf->pHead = NewHead;

out:
	KeReleaseSpinLockFromDpcLevel(&(pRingBuf->SplockReadWrite));
	KeLowerIrql(OldIrql);

	return Err;
}


INT 
RBRead(
	_REF_ PRINGBUFFER pRingBuf, 
	OUT   PCHAR pBuf, 
	_REF_ PULONG pSize
) {
	INT Result = ERROR_SUCCESS;
	if (!pRingBuf || !pSize) {
		Result = ERROR_BAD_ARGUMENTS;
		goto out;
	}
	
	PCHAR Head = pRingBuf->pHead;
	PCHAR Tail = pRingBuf->pTail;

	PCHAR NewTail;
	
	ULONG Size = RBSize(Head, Tail, pRingBuf->Capacity);
	ULONG RetSize = (*pSize < Size) ? *pSize : Size;
	

	if (Head >= Tail) {
		RtlCopyMemory(pBuf, Tail, RetSize);
		NewTail = Tail + RetSize;

	}
	else {
		ULONG DistToFlush = pRingBuf->Capacity - (Tail - pRingBuf->pData);
		if (RetSize <= DistToFlush) {
			RtlCopyMemory(pBuf, Tail, RetSize);
			NewTail = Tail + RetSize;
		}
		else {
			RtlCopyMemory(pBuf, Tail, DistToFlush);
			RtlCopyMemory(pBuf + DistToFlush, pRingBuf->pData, RetSize - DistToFlush);
			NewTail = pRingBuf->pData + RetSize - DistToFlush;
		}
	}
	
	*pSize = RetSize;
	pRingBuf->pTail = NewTail;
out:
	return Result;
}


INT 
RBLoadFactor(
	IN PRINGBUFFER  pRingBuf
) {
	return (INT)(100 * RBSize(pRingBuf->pHead, pRingBuf->pTail, pRingBuf->Capacity)) / pRingBuf->Capacity;
}