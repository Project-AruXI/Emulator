#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdbool.h>

#include "diagnostics.h"

#ifndef DEBUG
#define DEBUG_L DB_NONE
#else
#define DEBUG_L DEBUG
#endif

bool useColor;
int DebugLevel = DEBUG_L;
static FILE* debugStream;
static char buffer[150];

static char* errnames[D_ERR_DLIB+1] = {
	"",
	"INTERNAL ERROR",
	"IO ERROR",
	"SIGNAL ERROR",
	"SHARED MEMORY ERROR",
	"MEMORY ERROR",

	"INVALID FORMAT ERROR",
	"INVALID KERNEL ERROR",
	"DYNAMIC LIBRARY ERROR"
};

void initDiagnostics(FILE* stream, char* debugFile) {
	useColor = isatty(fileno(stream));
	debugStream = fopen(debugFile, "w");
	if (!debugStream) debugStream = stderr;
}

static void formatMessage(const char* fmsg, va_list args) {
	vsnprintf(buffer, 150, fmsg, args);
}

void dLog(errType err, sevType sev, const char* fmsg, ...) {
	va_list args;
	va_start(args, fmsg);

	formatMessage(fmsg, args);

	const char* prefix = NULL;
	const char* color = "";
	const char* reset = "";
	FILE* stream = stdout;

	switch (sev)	{
		case DSEV_INFO: prefix = "[INFO]"; break;
		case DSEV_WARN: prefix = "[WARN]"; break;
		case DSEV_FATAL: prefix = "[FATAL]"; break;
		default: prefix = "[]"; break;
	}

	if (useColor) {
		reset = RESET;
		switch (sev)	{
			case DSEV_WARN: color = YELLOW; break;
			case DSEV_FATAL: color = RED; break;
			default: break;
		}
	}

	if (sev == DSEV_FATAL) stream = stderr;

	fprintf(stream, "%s%s %s: %s%s\n", color, prefix, errnames[err], buffer, reset);

	if (sev == DSEV_FATAL) exit(-1);
}

void dFatal(errType err, const char* fmsg, ...) {
	va_list args;
	va_start(args, fmsg);

	formatMessage(fmsg, args);

	const char* color = "";
	const char* reset = "";

	if (useColor) { color = RED; reset = RESET; }

	fprintf(stderr, "%s[FATAL] %s: %s%s\n", color, errnames[err], buffer, reset);

	exit(-1);
}

void dDebug(debugLevel level, const char* fmsg, ...) {
#ifdef DEBUG
	// fprintf(stderr, "Debug defined; level at %d for %d\n", DEBUG, level);
	if (DebugLevel < level) return;
	va_list args;
	va_start(args, fmsg);

	formatMessage(fmsg, args);

	fprintf(debugStream, "[DEBUG::%d]: %s\n", level, buffer);
#endif
}

void flushDebug() {
#ifdef DEBUG
	fflush(debugStream);
#endif
}