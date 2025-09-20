#ifndef _SHELL_IO_H_
#define _SHELL_IO_H_

#include <stdint.h>

#define ARU_STDOUT 0
#define ARU_STDIN 1

void writeConsole(uint32_t bufferOffset, uint32_t count);

void readConsole();

#endif