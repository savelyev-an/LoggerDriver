#pragma once
#include <ntddk.h>
/******************************************************************************************************************
* The KLogger Component include KLogger.h, KLogger.c, RingBugffer.h, RingBugffer.c
* Public methods are presented here 
*******************************************************************************************************************/

#define _REF_ // marked input&output param in method 
#define ONE_SECOND_TIMEOUT 10000000ll

/*
* Levels of saved messages use only for DEFAULT_LOG_DETAILS settings
*/

#define LOG_LEVEL_ERROR 0
#define LOG_LEVEL_DEBUG 1
#define LOG_LEVEL_TRACE 2

/*
* Details level for Logging - use in KLoggerSetDetails and for DEFAULT_LOG_LEVEL settings
*/
#define LOG_DETAILS_NO 0
#define LOG_DETAILS_TIME 1
#define LOG_DETAILS_MESSAGELEVEL 2
#define LOG_DETAILS_TIME_MESSAGELEVEL 3

/*
* Default settings
*/
#define DEFAULT_FILE_NAME		L"\\??\\C:\\drivers\\klogger.log"
#define FLUSH_THRESHOLD			50u // in percents, set by default
#define DEFAULT_RING_BUF_SIZE	(100ull * 1024ull * 1024ull) // only if it is not derectly given
#define DEFAULT_FLUSH_TIMEOUT	(1*ONE_SECOND_TIMEOUT)
#define DEFAULT_LOG_LEVEL		LOG_LEVEL_TRACE
#define DEFAULT_LOG_DETAILS		LOG_DETAILS_TIME_MESSAGELEVEL

/*
* Initialising of the Logger
* fileName - the path to store the log, can be NULL - DEFAULT_FILE_NAME will be used
* bufferSize - the size of the buffer
*/
INT KLoggerInit(IN PUNICODE_STRING pFileName, IN ULONG bufferSize);

/*
* deinitialazation of the Logger - only at that time the file will be released
*/
VOID KLoggerDeinit();

/*
* Logger call level of message = ERROR
*/
INT  KLoggerLogError(IN PCSTR LogMsg);

/*
* Logger call level of message = DEBUG
*/
INT  KLoggerLogDebug(IN PCSTR LogMsg);

/*
* Logger call level of message = TRACE
*/
INT  KLoggerLogTrace(IN PCSTR LogMsg);


/*
* Set FlushTimeout
*/
VOID KLoggerSetFlashTimeout(IN INT Seconds);  

/*
* Set level of log
*/
VOID KLoggerSetLevel(IN INT LogLevel);

/*
* Set level of details for Log, possible values:
* LOG_DETAILS_NO, LOG_DETAILS_TIME, LOG_DETAILS_LEVEL, LOG_DETAILS_TIME_LEVEL 
*/
VOID KLoggerSetDetails(IN INT LogDetails);