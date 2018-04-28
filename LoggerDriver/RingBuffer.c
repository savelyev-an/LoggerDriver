#include "RingBuffer.h"

#include <winerror.h>


typedef struct RingBuffer {
	PCHAR pData; // the pointer to the 
	PCHAR volatile pHead; // the pointer to the next free element
	PCHAR volatile pTail; // the pointer to the first written element

	ULONG Capacity; // the length of the buffer

	KSPIN_LOCK SplockWrite; // only one Thread can write at the moment
	KSPIN_LOCK SpLockRead; // only one Thread can read at the moment

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

	KeInitializeSpinLock(&(RingBuf->SplockWrite));
	KeInitializeSpinLock(&(RingBuf->SpLockRead));

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
	IN PRINGBUFFER pRingBuf
) {
	if (pRingBuf->pHead >= pRingBuf->pTail) {
		return (ULONG)(pRingBuf->pHead - pRingBuf->pTail);

	} else {
		return (ULONG)(pRingBuf->Capacity - (pRingBuf->pTail - pRingBuf->pHead));
	}
}

static ULONG
RBFreeSize(
	IN PRINGBUFFER pRingBuf
) {
	return pRingBuf->Capacity - RBSize(pRingBuf);
}

static INT
RingDataWrite(
	IN PCHAR SrcBuf,
	ULONG SrcBufSize,
	PCHAR Data,
	ULONG Capacity,
	PCHAR Head,
	PCHAR Tail,
	PCHAR* NewHead
) {
	if (Head >= Tail) {
		ULONG DistToFinish = Capacity - (Head - Data);
		if (SrcBufSize > DistToFinish) {
			RtlCopyMemory(Head, SrcBuf, DistToFinish);
			RtlCopyMemory(Data, SrcBuf + DistToFinish, SrcBufSize - DistToFinish);
			*NewHead = Data + SrcBufSize - DistToFinish;

		} else {
			RtlCopyMemory(Head, SrcBuf, SrcBufSize);
			*NewHead = Head + SrcBufSize;
		}

	} else {
		RtlCopyMemory(Head, SrcBuf, SrcBufSize);
		*NewHead = Head + SrcBufSize;
	}

	return ERROR_SUCCESS;
}

ULONG 
RBWrite(
	_REF_ PRINGBUFFER pRingBuf, 
	IN PCHAR pBuf, 
	IN ULONG Size
) {
	if (!pRingBuf) {
		return ERROR_BAD_ARGUMENTS;
	}

	KIRQL OldIrql;
	KeRaiseIrql(HIGH_LEVEL, &OldIrql);
	KeAcquireSpinLockAtDpcLevel(&(pRingBuf->SplockWrite));

	PCHAR Head = pRingBuf->pHead;
	PCHAR Tail = pRingBuf->pTail;

	int Err;
    if (Size > RBFreeSize(pRingBuf) ){
		Err = ERROR_INSUFFICIENT_BUFFER;
		goto out;
	}

	PCHAR NewHead;
	Err = RingDataWrite(
			pBuf, 
			Size, 
			pRingBuf->pData, 
			pRingBuf->Capacity,
			Head,
			Tail, 
			&NewHead
	);
	if (Err != ERROR_SUCCESS) {
		goto out;
	}

	pRingBuf->pHead = NewHead;

out:
	KeReleaseSpinLockFromDpcLevel(&(pRingBuf->SplockWrite));
	KeLowerIrql(OldIrql);

	return Err;
}

static INT 
RingDataRead(
	PRINGBUFFER pRingBuf,
	PCHAR pDstBuf,
	ULONG DstBufSize,
	PCHAR Data,
	ULONG Capacity,
	PCHAR Head,
	PCHAR Tail,
	PULONG pRetSize,
	PCHAR* NewTail
) {
	ULONG Size = RBSize(pRingBuf);
	ULONG RetSize = (DstBufSize < Size) ? DstBufSize : Size;
	*pRetSize = RetSize;

	if (Head >= Tail) {
		RtlCopyMemory(pDstBuf, Tail, RetSize);
		*NewTail = Tail + RetSize;

	} else {
		ULONG DistToFlush = Capacity - (Tail - Data);
		if (RetSize <= DistToFlush) {
			RtlCopyMemory(pDstBuf, Tail, RetSize);
			*NewTail = Tail + RetSize;

		} else {
			RtlCopyMemory(pDstBuf, Tail, DistToFlush);
			RtlCopyMemory(pDstBuf + DistToFlush, Data, RetSize - DistToFlush);
			*NewTail = Data + RetSize - DistToFlush;
		}
	}

	return ERROR_SUCCESS;
}

// there is only one reader - fluhsing thread -> no sync
INT 
RBRead(
	_REF_ PRINGBUFFER pRingBuf, 
	OUT   PCHAR pBuf, 
	_REF_ PULONG pSize
) {
	if (!pRingBuf || !pSize) {
		return ERROR_BAD_ARGUMENTS;
	}

	KIRQL OldIrql;
	KeRaiseIrql(HIGH_LEVEL, &OldIrql);
	KeAcquireSpinLockAtDpcLevel(&(pRingBuf->SpLockRead));

	PCHAR Head = pRingBuf->pHead;
	PCHAR Tail = pRingBuf->pTail;

	ULONG RetSize;
	PCHAR NewTail;
	int Err = RingDataRead(
				pRingBuf,
				pBuf, 
				*pSize, 
				pRingBuf->pData, 
				pRingBuf->Capacity, 
				Head, 
				Tail, 
				&RetSize, 
				&NewTail
	);
	if (Err != ERROR_SUCCESS) {
		goto out;
	}

	*pSize = RetSize;
	pRingBuf->pTail = NewTail;
out:
	KeReleaseSpinLockFromDpcLevel(&(pRingBuf->SpLockRead));
	KeLowerIrql(OldIrql);

	return Err;
}

INT 
RBLoadFactor(
	IN PRINGBUFFER pRingBuf
) {
	PCHAR Head = pRingBuf->pHead;
	PCHAR Tail = pRingBuf->pTail;
	return (INT)(100 * RBSize(pRingBuf)) / pRingBuf->Capacity;
}
