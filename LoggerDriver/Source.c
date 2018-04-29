#include <ntddk.h>
#include <ntdef.h>
#include <WinError.h>

#include "KLogger.h"


HANDLE ThreadHandle;
PKTHREAD pThread; // to have check the end of routine in case of "stop driver"


NTSTATUS DriverEntry(
	_In_ struct _DRIVER_OBJECT *DriverObject, 
	_In_ PUNICODE_STRING RegistryPath
);

VOID DriverUnload(
	_In_ struct _DRIVER_OBJECT *DriverObject
);

PCHAR Message[] = {
	"Very Long Message 123456789012345678901234567890123456789012345678901234567890\r\n",
	"[klogtest 1]: curIRQL == 1\r\n",
	"[klogtest 1]: curIRQL == 2\r\n",
	"[klogtest 1]: curIRQL == 3\r\n",
	"[klogtest 1]: curIRQL == 4\r\n",
	"[klogtest 1]: curIRQL == 5\r\n",
	"[klogtest 1]: curIRQL == 6\r\n",
	"[klogtest 1]: curIRQL == 7\r\n",
	"[klogtest 1]: curIRQL == 8\r\n",
	"[klogtest 1]: curIRQL == 9\r\n",
	"[klogtest 1]: curIRQL == 10\r\n",
	"[klogtest 1]: curIRQL == 11\r\n",
	"[klogtest 1]: curIRQL == 12\r\n",
	"[klogtest 1]: curIRQL == 13\r\n",
	"[klogtest 1]: curIRQL == 14\r\n",
	"[klogtest 1]: curIRQL == 15\r\n"
};


VOID ThreadFunc(
	IN PVOID _Unused
) {
	UNREFERENCED_PARAMETER(_Unused);
	
	DbgPrint("Set Log Level = ERROR \n");
	KLoggerSetLevel(LOG_LEVEL_ERROR);
	DbgPrint("Log Message ERROR \n");
	KLoggerLogError("Log Message ERROR \r\n");
	DbgPrint("Log Message DEBUG \n");
	KLoggerLogDebug("Log Message DEBUG \r\n");
	KLoggerSetLevel(LOG_LEVEL_TRACE);


#if 1
	DbgPrint("1 Fast path: flushing by the buffer free space");
	
	// default level details
	// set timeout 10 sec, 
	KLoggerSetFlashTimeout(10);		

	KIRQL StartIrql = KeGetCurrentIrql();

	for (KIRQL curIrql = StartIrql; curIrql <= HIGH_LEVEL; ++curIrql) {

		DbgPrint("curIrql = %d",curIrql);

		KIRQL _OldIrql;
		KeRaiseIrql(curIrql, &_OldIrql);
		INT LogStat = KLoggerLogTrace(Message[curIrql]);
		DbgPrint("[klogtest 1]: curIRQL == %d, status: %d, message: %s", 
			curIrql, 
			LogStat,
			Message[curIrql]
		);
		KeLowerIrql(StartIrql);
	}


	LARGE_INTEGER DueTime;
	DueTime.QuadPart = -ONE_SECOND_TIMEOUT;	// 10^7 * 100us = 1; relative value
	LARGE_INTEGER	Interval;
	Interval.QuadPart = -2 * ONE_SECOND_TIMEOUT;

	KeDelayExecutionThread(KernelMode, FALSE, &Interval);
	DbgPrint("2 Slow path: TIMEOUT flushing all messages\n");
	// set no details
	KLoggerSetDetails(LOG_DETAILS_NO);
	// set timeout 1 sec
	KLoggerSetFlashTimeout(1);				

	for (KIRQL curIrql = StartIrql; curIrql <= HIGH_LEVEL; ++curIrql) {
		KIRQL _OldIrql;
		KeRaiseIrql(curIrql, &_OldIrql);
		INT LogStat = KLoggerLogTrace(Message[curIrql]);
		DbgPrint("[klogtest 1]: curIRQL == %d, status: %d, message: %s",
			curIrql,
			LogStat,
			Message[curIrql]
		);

		KeLowerIrql(StartIrql);
		KeDelayExecutionThread(KernelMode, FALSE, &Interval);
	}

	KeDelayExecutionThread(KernelMode, FALSE, &Interval);

	// set no details
	KLoggerSetDetails(LOG_DETAILS_MESSAGELEVEL);
	DbgPrint("3 Combined path: DPC (1.9 msg) and TIMEOUT (1.1 msg) flushing messages");

	for (KIRQL curIrql = StartIrql; curIrql <= HIGH_LEVEL; ++curIrql) {
		KIRQL _OldIrql;
		KeRaiseIrql(curIrql, &_OldIrql);
		INT LogStat = KLoggerLogTrace(Message[curIrql]);
		DbgPrint("[klogtest 1]: curIRQL == %d, status: %d, message: %s",
			curIrql,
			LogStat,
			Message[curIrql]
		);

		KeLowerIrql(StartIrql);
		if (curIrql % 3 == 0)
			KeDelayExecutionThread(KernelMode, FALSE, &Interval);
	}
#endif
	PsTerminateSystemThread(ERROR_SUCCESS);
}


NTSTATUS 
DriverEntry(
	IN struct _DRIVER_OBJECT *DriverObject,
	IN PUNICODE_STRING       RegistryPath
) {
	UNREFERENCED_PARAMETER(RegistryPath);
	
	
	//UNICODE_STRING fileName;  ;
	//RtlInitUnicodeString(&fileName, L"\\??\\C:\\drivers\\klogger.log");
	//KLoggerInit(&fileName, 50);
	KLoggerInit(NULL, 50);


	DbgPrint("[test_driver_1]: 'DriverEntry()' is executed");
	DriverObject->DriverUnload = DriverUnload;

	DbgPrint("[test_driver_1]: 'PsCreateSystemThread()' is started");
	NTSTATUS status = PsCreateSystemThread(
		&ThreadHandle,
		THREAD_ALL_ACCESS,
		NULL,
		NULL,
		NULL,
		ThreadFunc,
		NULL);
	DbgPrint("[test_driver_1]: 'PsCreateSystemThread()' is finished");

	if (NT_SUCCESS(status)) {
		DbgPrint("[test_driver_1]: 'ObReferenceObjectByHandle()' is started");
		status = ObReferenceObjectByHandle(
			ThreadHandle,
			FILE_ANY_ACCESS,
			NULL,
			KernelMode,
			(PVOID *) &(pThread),
			NULL);
		DbgPrint("[test_driver_1]: 'ObReferenceObjectByHandle()' is finished, status %d", status);

	}
	else {
		DbgPrint("[test_driver_1]: error exit");
		return ERROR_TOO_MANY_TCBS;
	}

	DbgPrint("[klogger_test_1]: 'DriverEntry()' finished");

		return STATUS_SUCCESS;
}

VOID 
DriverUnload(
	IN struct _DRIVER_OBJECT *DriverObject
) {
	UNREFERENCED_PARAMETER(DriverObject);
	DbgPrint("[test_driver_1]: 'DriverUnload()' is started");
	DbgPrint("[test_driver_1]: 'KeWaitForSingleObject()' is started");

	// Wait till the main thread will finish
	KeWaitForSingleObject(
		pThread,
		Executive,
		KernelMode,
		FALSE,
		NULL
	);

	KLoggerDeinit();

	DbgPrint("[test_driver_1]: 'KeWaitForSingleObject()' is finished");

	ObDereferenceObject(pThread);

	ZwClose(ThreadHandle);

	DbgPrint("[test_driver_1]: 'DriverUnload()' is finished");
	return;
}