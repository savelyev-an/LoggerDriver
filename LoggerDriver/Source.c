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


VOID ThreadFunc(
	IN PVOID _Unused
) {

	UNREFERENCED_PARAMETER(_Unused);

	PCHAR Message[] = {
	"Very Long Message 123456789012345678901234567890123456789012345678901234567890\r\n",
	//"[klogtest 1]: curIRQL == 0\r\n",
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

	
	DbgPrint("1 Fast path: DPC flushing first part of messages");
	KIRQL StartIrql = KeGetCurrentIrql();
	
	//__debugbreak();

	for (KIRQL curIrql = StartIrql; curIrql <= HIGH_LEVEL; ++curIrql) {

		DbgPrint("curIrql = %d",curIrql);

		KIRQL _OldIrql;
		KeRaiseIrql(curIrql, &_OldIrql);
		INT LogStat = KLoggerLog(Message[curIrql]);
		DbgPrint("[klogtest 1]: curIRQL == %d, status: %d, message: %s", 
			curIrql, 
			LogStat,
			Message[curIrql]
		);

		KeLowerIrql(StartIrql);
	}
#if 1
	LARGE_INTEGER DueTime;
	DueTime.QuadPart = -10000000LL;	// 10^7 * 100us = 1; relative value
	LARGE_INTEGER	Interval;
#define FLUSH_TIMEOUT 10000000ll
	Interval.QuadPart = -2 * FLUSH_TIMEOUT;

	KeDelayExecutionThread(KernelMode, FALSE, &Interval);
	DbgPrint("1 ...\n");
	DbgPrint("1 ...\n");
	DbgPrint("1 Slow path: TIMEOUT flushing all messages\n");

	for (KIRQL curIrql = StartIrql; curIrql <= HIGH_LEVEL; ++curIrql) {
		KIRQL _OldIrql;
		KeRaiseIrql(curIrql, &_OldIrql);
		INT LogStat = KLoggerLog(Message[curIrql]);
		DbgPrint("[klogtest 1]: curIRQL == %d, status: %d, message: %s",
			curIrql,
			LogStat,
			Message[curIrql]
		);

		KeLowerIrql(StartIrql);
		KeDelayExecutionThread(KernelMode, FALSE, &Interval);
	}

	KeDelayExecutionThread(KernelMode, FALSE, &Interval);


	DbgPrint("1 ...");
	DbgPrint("1 ...");
	DbgPrint("1 Combined path: DPC (1.9 msg) and TIMEOUT (1.1 msg) flushing messages");

	for (KIRQL curIrql = StartIrql; curIrql <= HIGH_LEVEL; ++curIrql) {
		KIRQL _OldIrql;
		KeRaiseIrql(curIrql, &_OldIrql);
		INT LogStat = KLoggerLog(Message[curIrql]);
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
	
	
	UNICODE_STRING fileName;  ;
	RtlInitUnicodeString(&fileName, L"\\??\\C:\\drivers\\klogger.log");
	KLoggerInit(&fileName, 50);

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