#pragma once

#include <ntddk.h>

//#define LOG_FILE_NAME L"\\??\\C:\\drivers\\klogger.log"
#define _REF_ // marked input&output param in method 


typedef struct KLogger* PKLOGGER;

INT KLoggerInit(IN PUNICODE_STRING RegistryPath, IN ULONG bufferSize);
VOID KLoggerDeinit();
INT  KLoggerLog(IN PCSTR log_msg);
