#include <WinError.h>
#include "MyNtstrsafe.h"
#include "RingBuffer.h"
#include "KLogger.h"


/*
* Level of saved messages
*/

#define LOG_LEVEL_ERROR 0
#define LOG_LEVEL_DEBUG 1
#define LOG_LEVEL_TRACE 2

typedef struct KLogger
{
	PRINGBUFFER pRingBuf; // the RingBuffer

	HANDLE FileHandle; // file Handle for writing
	PCHAR pFlushingBuf; // flushing Buffer

	HANDLE FlushingThreadHandle; // Flushing thread handle
	PKTHREAD pFlushingThread;    // Pointer to Flushing thread 

	KEVENT FlushEvent;  // event to flash data to file
	KEVENT StartFlushingThreadEvent; // event FlushingThread has been started
	KEVENT StopEvent;   // event to stop flushing thread
	KEVENT FlushingIsDone; // event to continue writing long message 

	LONG volatile IsFlushDispatched; // Is Flush already dispatched 
	ULONG FlushBufferSize;           

	LARGE_INTEGER TimeoutFlash; 

	INT LogLevel;
	INT LogDetails;
	
} KLOGGER;

typedef struct KLogger* PKLOGGER;

PKLOGGER gKLogger;

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
			&gKLogger->TimeoutFlash,
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
	IN PUNICODE_STRING pFileName,
	IN ULONG bufferSize
) {
	int Err = ERROR_SUCCESS;
	__debugbreak();
	// allocate memory for the LoggerStructure
	gKLogger = (PKLOGGER)ExAllocatePool(NonPagedPool, sizeof(KLOGGER));
	if (gKLogger == NULL) {
		Err = ERROR_NOT_ENOUGH_MEMORY;
		goto err_klogger_mem;
	}
	

	// Initialize the RingBuffer
	if (bufferSize==0) bufferSize= DEFAULT_RING_BUF_SIZE;
	Err = RBInit(&(gKLogger->pRingBuf), bufferSize);
	gKLogger->FlushBufferSize = bufferSize;

	if (Err != ERROR_SUCCESS) {
		goto err_ring_buf_init;
	}
	
	gKLogger->TimeoutFlash.QuadPart = -DEFAULT_FLUSH_TIMEOUT;

	// Initialize Events
	KeInitializeEvent(&(gKLogger->FlushEvent), SynchronizationEvent, FALSE);
	KeInitializeEvent(&(gKLogger->StartFlushingThreadEvent), SynchronizationEvent, FALSE);
	KeInitializeEvent(&(gKLogger->StopEvent), SynchronizationEvent, FALSE);
	KeInitializeEvent(&(gKLogger->FlushingIsDone), SynchronizationEvent, FALSE);

	// Initializing 
	gKLogger->IsFlushDispatched = 0;
	gKLogger->LogLevel = DEFAULT_LOG_LEVEL;
	gKLogger->LogDetails = DEFAULT_LOG_DETAILS;

	// alloc buffer for flushing thread
	gKLogger->pFlushingBuf = (PCHAR)ExAllocatePool(PagedPool, gKLogger->FlushBufferSize * sizeof(CHAR));
	if (!gKLogger->pFlushingBuf) {
		Err = ERROR_NOT_ENOUGH_MEMORY;
		goto err_flush_mem;
	}

	// open file for flushing thread
	OBJECT_ATTRIBUTES ObjAttr;
	IO_STATUS_BLOCK IoStatusBlock;
	
	UNICODE_STRING FileNameFinal; 

	if (!pFileName) {
		RtlInitUnicodeString(&FileNameFinal, DEFAULT_FILE_NAME);
	}
	else 
	{
		FileNameFinal = *pFileName;
	}
	
	InitializeObjectAttributes(
		&ObjAttr,
		&FileNameFinal,
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
	#define START_TIMEOUT 50000000ll
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

/*
* support function
*/
static ULONG 
StrLen(
	PCSTR Str
) {
	ULONG Length = 0;
	while (*(Str + Length) != '\0') {
		Length++;
		if (Length == 2000000000) {
			return  0; //Error - not a zero end string
		}
	}
	return Length;
}

static
INT 
KLoggerLog(
	IN PCSTR LogMsg
) {
	if (!gKLogger) return ERROR_OBJECT_NO_LONGER_EXISTS;

	ULONG OldMessageLength = StrLen(LogMsg);
	ULONG MessageLength = OldMessageLength;
	INT Err = ERROR_SUCCESS;

	while (MessageLength != 0) {
		DbgPrint("Try to write length=%d\n", MessageLength);
		
		Err = RBWrite(gKLogger->pRingBuf, LogMsg, &MessageLength);
		DbgPrint("Still has to write length=%d\n", MessageLength);

		int LoadFactor = RBLoadFactor(gKLogger->pRingBuf);
		DbgPrint("Load factor: %d\n", LoadFactor);

		if (LoadFactor >= FLUSH_THRESHOLD) {
			LONG isFluchDispatched = InterlockedCompareExchange(&(gKLogger->IsFlushDispatched), 1, 0);
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
	return Err;
}

static 
void  
CheckStamping(
IN  INT* size,
OUT INT* newSize,
OUT PBOOLEAN TimePrint, 
OUT PBOOLEAN LevelPrint 
){
#define TIME_STAMP_SIZE 30
#define LEVEL_STAMP_SIZE 10
	*TimePrint = FALSE;
	*LevelPrint = FALSE;
	switch (gKLogger->LogDetails) {
	case LOG_DETAILS_NO:
		break;
	case LOG_DETAILS_TIME:
		*TimePrint = TRUE;
		*newSize = *size + TIME_STAMP_SIZE;
		break;
	case LOG_DETAILS_MESSAGELEVEL:
		*LevelPrint = TRUE;
		*newSize = *size + LEVEL_STAMP_SIZE;
		break;
	case LOG_DETAILS_TIME_MESSAGELEVEL:
		*TimePrint = TRUE;
		*LevelPrint = TRUE;
		*newSize = *size + LEVEL_STAMP_SIZE + TIME_STAMP_SIZE;
		break;
	}
}

static
INT
KLoggerLogDetails(
	IN  PCSTR LogMsg,
	IN  PCSTR LevelStamp ) 
{
	INT Result=0;
	size_t size = StrLen(LogMsg); 

	BOOLEAN TimePrint;
	BOOLEAN LevelPrint;
	size_t newSize;
	CheckStamping(&size, &newSize, &TimePrint, &LevelPrint);

	if (TimePrint || LevelPrint) {
		PCHAR FullMessage = (PCHAR)ExAllocatePool(PagedPool, newSize * sizeof(CHAR));
		PCHAR ZeroMessage = "";
		RtlStringCbCopyA(FullMessage, newSize, ZeroMessage);
		if (TimePrint) {
			char timeStr[40];
			TIME_FIELDS TimeFields;
			LARGE_INTEGER time;
			KeQuerySystemTime(&time);
			ExSystemTimeToLocalTime(&time, &time);
			RtlTimeToTimeFields(&time, &TimeFields);
			RtlStringCbPrintfA(timeStr, sizeof(timeStr), "[%04d.%02d.%02d - %02d:%02d]",
				TimeFields.Year, TimeFields.Month, TimeFields.Day,
				TimeFields.Hour, TimeFields.Minute);
			RtlStringCbCatA(FullMessage, newSize, timeStr);
		}
		if (LevelPrint) {
			RtlStringCbCatA(FullMessage, newSize, LevelStamp);
		}
		RtlStringCbCatA(FullMessage, newSize, LogMsg);
		Result = KLoggerLog(FullMessage);
		DbgPrint("New Message:  %s\n", FullMessage);
		ExFreePool(FullMessage);
	}
	else
	{
		Result = KLoggerLog(LogMsg);
	}
	return Result;
}


VOID KLoggerSetFlashTimeout(IN INT Seconds)
{
	gKLogger->TimeoutFlash.QuadPart = - Seconds * ONE_SECOND_TIMEOUT;
}

VOID KLoggerSetLevel(IN INT LogLevel)
{
	if (LogLevel >= LOG_LEVEL_ERROR && LogLevel <= LOG_LEVEL_TRACE)
		gKLogger->LogLevel = LogLevel;
}

VOID KLoggerSetDetails(IN INT LogDetails)
{
	if (LogDetails >= LOG_DETAILS_NO && LogDetails <= LOG_DETAILS_TIME_MESSAGELEVEL)
		gKLogger->LogDetails = LogDetails;
}


INT KLoggerLogError(IN PCSTR log_msg)
{	
	INT result = ERROR_SUCCESS;
	if (gKLogger->LogLevel >= LOG_LEVEL_ERROR)
		result = KLoggerLogDetails(log_msg, "[ERROR]");
	return result;
}	


INT KLoggerLogDebug(IN PCSTR log_msg)
{
	INT result = ERROR_SUCCESS;
	if (gKLogger->LogLevel >= LOG_LEVEL_DEBUG)
		result = KLoggerLogDetails(log_msg, "[DEBUG]");
	return result;
}

INT KLoggerLogTrace(IN PCSTR log_msg)
{
	INT result = ERROR_SUCCESS;
	if (gKLogger->LogLevel >= LOG_LEVEL_TRACE)
		result = KLoggerLogDetails(log_msg, "[TRACE]");
	return result;
}
