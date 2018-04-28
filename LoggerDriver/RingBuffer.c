#include "RingBuffer.h"

#include <winerror.h>

INT
RBInit(
	PRINGBUFFER* pRingBuf,
	ULONG Size
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


	RingBuf->Data = (PCHAR)ExAllocatePool(NonPagedPool, Size * sizeof(CHAR));
	if (!RingBuf->Data) {
		Err = ERROR_NOT_ENOUGH_MEMORY;
		ExFreePool(RingBuf);
		goto err_ret;
	}

	RingBuf->Head = RingBuf->Data;
	RingBuf->Tail = RingBuf->Data;
	RingBuf->Capacity = Size;


	KeInitializeSpinLock(&(RingBuf->SplockHead));
	KeInitializeSpinLock(&(RingBuf->SplockTail));
	KeInitializeSpinLock(&(RingBuf->SplockWrite));

err_ret:
	return Err;
}

INT
RBDeinit(
	PRINGBUFFER pRingBuf
) {
	if (!pRingBuf) {
		return ERROR_BAD_ARGUMENTS;
	}

	ExFreePool(pRingBuf->Data);
	ExFreePool(pRingBuf);

	return ERROR_SUCCESS;
}

static VOID
SpinlockExchange(
	PCHAR* pSrc,
	PCHAR* pDst, 
	PKSPIN_LOCK Splock
) {
	KIRQL OldIrql;

	KeRaiseIrql(HIGH_LEVEL, &OldIrql);
	KeAcquireSpinLockAtDpcLevel(Splock);

	*pDst = *pSrc;

	KeReleaseSpinLockFromDpcLevel(Splock);
	KeLowerIrql(OldIrql);
} 

ULONG
RBSize(
	PCHAR Head,
	PCHAR Tail,
	ULONG Capacity
) {
	if (Head >= Tail) {
		return (ULONG)(Head - Tail);

	} else {
		return (ULONG)(Capacity - (Tail - Head));
	}
}

static ULONG
RBFreeSize(
	PCHAR Head,
	PCHAR Tail,
	ULONG Capacity
) {
	return Capacity - RBSize(Head, Tail, Capacity);
}

static INT
RingDataWrite(
	PCHAR SrcBuf,
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

INT 
RBWrite(
	PRINGBUFFER pRingBuf, 
	PCHAR pBuf, 
	ULONG Size
) {
	if (!pRingBuf) {
		return ERROR_BAD_ARGUMENTS;
	}

	KIRQL OldIrql;
	KeRaiseIrql(HIGH_LEVEL, &OldIrql);
	KeAcquireSpinLockAtDpcLevel(&(pRingBuf->SplockWrite));

	PCHAR Head, Tail; // get value with spinlock and then use copy
	SpinlockExchange(&(pRingBuf->Head), &Head, &(pRingBuf->SplockHead));
	SpinlockExchange(&(pRingBuf->Tail), &Tail, &(pRingBuf->SplockTail));

	int Err;
	if (Size > RBFreeSize(Head, Tail, pRingBuf->Capacity)) {
		Err = ERROR_INSUFFICIENT_BUFFER;
		goto out;
	}

	PCHAR NewHead;
	Err = RingDataWrite(
			pBuf, 
			Size, 
			pRingBuf->Data, 
			pRingBuf->Capacity,
			Head,
			Tail, 
			&NewHead
	);
	if (Err != ERROR_SUCCESS) {
		goto out;
	}

	SpinlockExchange(&NewHead, &(pRingBuf->Head), &(pRingBuf->SplockHead));

out:
	KeReleaseSpinLockFromDpcLevel(&(pRingBuf->SplockWrite));
	KeLowerIrql(OldIrql);

	return Err;
}

static INT 
RingDataRead(
	PCHAR pDstBuf,
	ULONG DstBufSize,
	PCHAR Data,
	ULONG Capacity,
	PCHAR Head,
	PCHAR Tail,
	PULONG pRetSize,
	PCHAR* NewTail
) {
	ULONG Size = RBSize(Head, Tail, Capacity);
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
	PRINGBUFFER pRingBuf, 
	PCHAR pBuf, 
	PULONG pSize
) {
	if (!pRingBuf || !pSize) {
		return ERROR_BAD_ARGUMENTS;
	}

	// TODO: res
	PCHAR Head, Tail; // get value with sync and then use copy
	SpinlockExchange(&(pRingBuf->Head), &Head, &(pRingBuf->SplockHead));
	SpinlockExchange(&(pRingBuf->Tail), &Tail, &(pRingBuf->SplockTail));

	ULONG RetSize;
	PCHAR NewTail;
	int Err = RingDataRead(
				pBuf, 
				*pSize, 
				pRingBuf->Data, 
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
	SpinlockExchange(&NewTail, &(pRingBuf->Tail), &(pRingBuf->SplockTail));

out:
	return Err;
}

INT 
RBLoadFactor(
	PRINGBUFFER pRingBuf
) {
	PCHAR Head, Tail; // get value with spinlock and then use copy
	SpinlockExchange(&(pRingBuf->Head), &Head, &(pRingBuf->SplockHead));
	SpinlockExchange(&(pRingBuf->Tail), &Tail, &(pRingBuf->SplockTail));

	return (INT)(100 * RBSize(Head, Tail, pRingBuf->Capacity)) / pRingBuf->Capacity;
}
