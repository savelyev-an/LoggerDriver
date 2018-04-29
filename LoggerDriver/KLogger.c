#include <WinError.h>
#include "RingBuffer.h"
#include "KLogger.h"

#define FLUSH_THRESHOLD 50u // in percents
#define DEFAULT_RING_BUF_SIZE (100ull * 1024ull * 1024ull) // only if it is not derectly given
#define REGISTRY_BUF_SIZE_KEY L"BUF_SIZE"
#define FLUSH_TIMEOUT 10000000ll
#define START_TIMEOUT 50000000ll

typedef struct KLogger
{
	PRINGBUFFER pRingBuf; // the RingBuffer

	HANDLE FileHandle; // file Handle for writing
	PCHAR pFlushingBuf; // flushing Buffer

	HANDLE FlushingThreadHandle; 
	PKTHREAD pFlushingThread;

	KEVENT FlushEvent;  // event to flash data to file
	KEVENT StartFlushingThreadEvent; // event FlushingThread has been started
	KEVENT StopEvent;   // event to stop flushing thread

	LONG volatile IsFlushDispatched;
	//PKDPC pFlushDpc;
	ULONG FlushBufferSize;
	KEVENT FlushingIsDone;
	//KSPIN_LOCK SpLockLog;

} KLOGGER;

PKLOGGER gKLogger;

VOID SetWriteEvent(
	IN PKDPC pthisDpcObject,
	IN PVOID DeferredContext,
	IN PVOID SystemArgument1,
	IN PVOID SystemArgument2
);

static INT 
WriteToFile(
	IN HANDLE FileHandle,
	IN PVOID Buf, 
	IN ULONG Length
) {
	IO_STATUS_BLOCK IoStatusBlock;
	NTSTATUS Status = ZwWriteFile(
		FileHandle,
		NULL,
		NULL,
		NULL,
		&IoStatusBlock,
		Buf,
		(ULONG)Length,
		NULL,
		NULL
	);

	return Status;
}

VOID 
FlushingThreadFunc(
	IN PVOID _Unused
) {
	UNREFERENCED_PARAMETER(_Unused);
	KeSetEvent(&(gKLogger->StartFlushingThreadEvent), 0, FALSE);

	PVOID handles[2];
	handles[0] = (PVOID)&(gKLogger->FlushEvent);
	handles[1] = (PVOID)&(gKLogger->StopEvent);

	LARGE_INTEGER Timeout;
	Timeout.QuadPart = -FLUSH_TIMEOUT;

	NTSTATUS Status, WriteStatus;
	ULONG Length = 0;
	while (TRUE) {
		Status = KeWaitForMultipleObjects(
			2,
			handles,
			WaitAny,
			Executive,
			KernelMode,
			TRUE,
			&Timeout,
			NULL);

		if (Status == STATUS_TIMEOUT)
			DbgPrint("Flushing thread is woken by TIMEOUT\n");

		if (Status == STATUS_WAIT_0) {
			KeClearEvent(&gKLogger->FlushEvent);
			DbgPrint("Flushing thread is woken by FLUSH EVENT\n");
		}

		if (Status == STATUS_WAIT_1) {
			KeClearEvent(&gKLogger->StopEvent);
			DbgPrint("Flushing thread is woken by STOP EVENT\n");
		}

		Length = gKLogger->FlushBufferSize;
		int Err = RBRead(gKLogger->pRingBuf, gKLogger->pFlushingBuf, &Length);
		if (Err == ERROR_SUCCESS) {
			WriteStatus = WriteToFile(gKLogger->FileHandle, gKLogger->pFlushingBuf, Length);
			if (WriteStatus != STATUS_SUCCESS) {
				DbgPrint("Error: can't write to log file, return code %d\n", WriteStatus);
			} 
		} else {
			DbgPrint("Error: can't read from ring_buffer, return code %d\n", Err);
		}

		KeSetEvent(&(gKLogger->FlushingIsDone), 0, FALSE);

		if (Status == STATUS_WAIT_1) {
			PsTerminateSystemThread(ERROR_SUCCESS); // exit
		}

		
	}
}
 
INT 
KLoggerInit(
	IN PUNICODE_STRING fileName,
	IN ULONG bufferSize
) {
	int Err = ERROR_SUCCESS;

	// allocate memory for the LoggerStructure
	gKLogger = (PKLOGGER)ExAllocatePool(NonPagedPool, sizeof(KLOGGER));
	if (gKLogger == NULL) {
		Err = ERROR_NOT_ENOUGH_MEMORY;
		goto err_klogger_mem;
	}

	//KeInitializeSpinLock(&(gKLogger->SpLockLog));

	// Initialize the RingBuffer
	if (bufferSize==0) bufferSize= DEFAULT_RING_BUF_SIZE;
	Err = RBInit(&(gKLogger->pRingBuf), bufferSize);
	gKLogger->FlushBufferSize = bufferSize;

	if (Err != ERROR_SUCCESS) {
		goto err_ring_buf_init;
	}

	// Initialize 
	KeInitializeEvent(&(gKLogger->FlushEvent), SynchronizationEvent, FALSE);
	KeInitializeEvent(&(gKLogger->StartFlushingThreadEvent), SynchronizationEvent, FALSE);
	KeInitializeEvent(&(gKLogger->StopEvent), SynchronizationEvent, FALSE);
	KeInitializeEvent(&(gKLogger->FlushingIsDone), SynchronizationEvent, FALSE);

	gKLogger->IsFlushDispatched = 0;

	// alloc buffer for flushing thread
	gKLogger->pFlushingBuf = (PCHAR)ExAllocatePool(PagedPool, gKLogger->FlushBufferSize * sizeof(CHAR));
	if (!gKLogger->pFlushingBuf) {
		Err = ERROR_NOT_ENOUGH_MEMORY;
		goto err_flush_mem;
	}


	// open file for flushing thread
	OBJECT_ATTRIBUTES ObjAttr;
	IO_STATUS_BLOCK IoStatusBlock;

	InitializeObjectAttributes(
		&ObjAttr,
		fileName,
		OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
		NULL,
		NULL
	);

	NTSTATUS Status = ZwCreateFile(
		&(gKLogger->FileHandle),
		FILE_APPEND_DATA,
		&ObjAttr,
		&IoStatusBlock,
		0,
		FILE_ATTRIBUTE_NORMAL,
		FILE_SHARE_WRITE,
		FILE_OPEN_IF,
		FILE_SYNCHRONOUS_IO_ALERT,
		NULL,
		0
	);

	if (!NT_SUCCESS(Status)) {
		Err = ERROR_CANNOT_MAKE;
		goto err_file;
	}

	Status = PsCreateSystemThread(
		&(gKLogger->FlushingThreadHandle),
		THREAD_ALL_ACCESS,
		NULL,
		NULL,
		NULL,
		FlushingThreadFunc,
		NULL);

	if (NT_SUCCESS(Status)) {
		Status = ObReferenceObjectByHandle(
			gKLogger->FlushingThreadHandle,
			FILE_ANY_ACCESS,
			NULL,
			KernelMode,
			(PVOID*)&(gKLogger->pFlushingThread),
			NULL);
	} else {
		Err = ERROR_TOO_MANY_TCBS;
		goto err_thread;
	}

	// wait while flushing thread start
	LARGE_INTEGER Timeout;
	Timeout.QuadPart = -START_TIMEOUT;

	KeWaitForSingleObject(
		&(gKLogger->StartFlushingThreadEvent),
		Executive,
		KernelMode,
		FALSE,
		&Timeout);

	return STATUS_SUCCESS;

err_thread:
	ZwClose(gKLogger->FileHandle);

err_file:
	ExFreePool(gKLogger->pFlushingBuf);

err_flush_mem:
	RBDeinit(gKLogger->pRingBuf);

err_ring_buf_init:
	ExFreePool(gKLogger);

err_klogger_mem:
	return Err;
}

VOID 
KLoggerDeinit() {
	// Provide an event to finish the flushing thread 
	KeSetEvent(&(gKLogger->StopEvent), 0, FALSE);

	// wait till the end of the flushing thread
	KeWaitForSingleObject(
		gKLogger->pFlushingThread,
		Executive,
		KernelMode,
		FALSE,
		NULL);

	KeFlushQueuedDpcs();

	ObDereferenceObject(gKLogger->pFlushingThread);
	ZwClose(gKLogger->FlushingThreadHandle);

	ExFreePool(gKLogger->pFlushingBuf);
	ZwClose(gKLogger->FileHandle);

	RBDeinit(gKLogger->pRingBuf);
	ExFreePool(gKLogger);
}


static ULONG 
StrLen(
	PCSTR Str
) {
	ULONG Length = 0;
	while (*(Str + Length) != '\0') {
		Length++;
	}
	return Length;
}

INT 
KLoggerLog(
	IN PCSTR LogMsg
) {
	if (!gKLogger) return ERROR_OBJECT_NO_LONGER_EXISTS;

	ULONG OldMessageLength = StrLen(LogMsg);
	ULONG MessageLength = OldMessageLength;
	INT Err = ERROR_SUCCESS;
	////Get Spinlock to prevent concurent logging
	//KIRQL OldIrql;
	//KeRaiseIrql(HIGH_LEVEL, &OldIrql);
	//KeAcquireSpinLockAtDpcLevel(&(gKLogger->SpLockLog));

	while (MessageLength != 0) {
		DbgPrint("Try to write length=%d\n", MessageLength);
		Err = RBWrite(gKLogger->pRingBuf, LogMsg, &MessageLength);
		DbgPrint("Still has to write length=%d\n", MessageLength);


		int LoadFactor = RBLoadFactor(gKLogger->pRingBuf);
		DbgPrint("Load factor: %d\n", LoadFactor);

		if (LoadFactor >= FLUSH_THRESHOLD) {
			//DbgPrint("Pre Interlocked: is flush dpc queued: %d", gKLogger->IsFlushDispatched);
			LONG isFluchDispatched = InterlockedCompareExchange(&(gKLogger->IsFlushDispatched), 1, 0);
			//DbgPrint("Post Interlocked original value: %d, is flush dpc queued: %d", OrigDst, gKLogger->IsFlushDispatched);
			if (!isFluchDispatched) {
				DbgPrint("Try to use FlashEvent\n");
				InterlockedExchange(&(gKLogger->IsFlushDispatched), 0);
				KeSetEvent(&gKLogger->FlushEvent, 0, FALSE);
			}
		}
		// proceed the case MessageLength> bufferFreeSize
		if (MessageLength > 0) {
			// поменять указатели
			LogMsg = LogMsg + OldMessageLength - MessageLength;
			OldMessageLength = StrLen(LogMsg);
			MessageLength = OldMessageLength;
			
			DbgPrint("New length=: %d, new Message====> %s \n", MessageLength, LogMsg);
			// wait flushing 
			NTSTATUS status = KeWaitForSingleObject(
				&(gKLogger->FlushingIsDone),
				Executive,
				KernelMode,
				FALSE,
				NULL); // TODO ERROR TIMEOUT 
			KeClearEvent(&gKLogger->FlushingIsDone);
		}

	}

	/*KeReleaseSpinLockFromDpcLevel(&(gKLogger->SpLockLog));
	KeLowerIrql(OldIrql);
	*/
	return Err;
}