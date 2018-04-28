#pragma once

#include <ntddk.h>

//#define LOG_FILE_NAME L"\\??\\C:\\drivers\\klogger.log"
#define _REF_ // marked input&output param in method 


typedef struct KLogger* PKLOGGER;

/*
* Initialising of the Logger
* fileName - the path to store the log
* bufferSize - the size of the buffer
*/
INT KLoggerInit(IN PUNICODE_STRING fileName, IN ULONG bufferSize);

/*
* deinitialazation of the Logger
*/
VOID KLoggerDeinit();

/*
* Logger call
*/
INT  KLoggerLog(IN PCSTR log_msg);

/*
* Stop the logger to see the file
*/
//VOID KLoggerStop();  // Release log_file, stop flushing
