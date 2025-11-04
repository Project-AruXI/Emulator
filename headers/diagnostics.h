#ifndef _DIAGNOSTICS_H_
#define _DIAGNOSTICS_H_

#include <stdio.h>

#define RESET "\x1b[0m"
#define RED "\x1b[31m"
#define GREEN "\x1b[32m"
#define YELLOW "\x1b[33m"
#define BLUE "\x1b[34m"
#define PURPLE "\x1b[35m"
#define CYAN "\x1b[36m"
#define WHITE "\x1b[37m" 

typedef enum {
	D_NONE, // Allow for normal logging
	D_ERR_INTERNAL,
	D_ERR_IO,
	D_ERR_SIGNAL,
	D_ERR_SHAREDMEM,
	D_ERR_MEM,

	// Specific types

	// Emulator
	D_ERR_INVALID_FORMAT,
	D_ERR_INVALID_KERNEL,
	D_ERR_DLIB
} errType;

typedef enum {
	DSEV_INFO, // Non-colored
	DSEV_WARN, // Colored?
	DSEV_FATAL // Colored?
} sevType;

typedef enum {
	DB_NONE,
	DB_BASIC,
	DB_DETAIL,
	DB_TRACE,
	DB_ALL
} debugLevel;

void initDiagnostics(FILE* stream, char* debugFile);

/**
 * 
 * @param err 
 * @param sev 
 * @param fmsg 
 * @param  
 */
void dLog(errType err, sevType sev, const char* fmsg, ...);

/**
 * 
 * @param err 
 * @param fmsg 
 * @param  
 */
void dFatal(errType err, const char* fmsg, ...); // just like log but more geared towards fatal

/**
 * 
 * @param level The minimum level that the message will appear
 * @param fmsg 
 * @param ... 
 */
void dDebug(debugLevel level, const char* fmsg, ...);

/**
 * 
 */
void flushDebug();

#endif