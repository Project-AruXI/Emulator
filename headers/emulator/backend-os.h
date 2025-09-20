#ifndef _BACKEND_OS_H_
#define _BACKEND_OS_H_

#include <stdint.h>


#define ARU_STDOUT 0
#define ARU_STDIN 1

typedef struct __attribute__((packed)) IO {
	int fd;
	uint32_t count;
	uint32_t buffer; // pointer provided by user
} io_md;


void ruWrite(const char* buffer, uint32_t count);

void ruRead(char* buffer, uint32_t count);

#endif