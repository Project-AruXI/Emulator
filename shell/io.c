#include <unistd.h>

#include "io.h"
#include "diagnostics.h"
#include "sigHeap.h"


void writeConsole(uint32_t bufferOffset, uint32_t count) {
	const char* buffer = (const char*) offsetToPtr(bufferOffset);

	write(STDOUT_FILENO, buffer, count)	;
}

void readConsole() {
}