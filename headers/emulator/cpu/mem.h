#ifndef _MEM_H_
#define _MEM_H_

#include <stdint.h>

#include "memsects.h"


typedef enum memerr {
	MEMERR_INTERNAL,
	MEMERR_NONE,

	MEMERR_KERN_OVERFLOW, // Overflowing into other sections
	MEMERR_KERN_OVERREAD, // Overreading into other sections
	// Kernel writing errors
	MEMERR_KERN_STACK_OVERFLOW,
	MEMERR_KERN_HEAP_OVERFLOW,
	MEMERR_KERN_TEXT_WRITE, // Writing to text section
	MEMERR_KERN_SECT_WRITE, // Writing to other sections not in kernel space

	// Kernel reading errors
	MEMERR_KERN_SECT_READ, // Reading from other sections that is not in kernel space or system libraries

	MEMERR_USER_OVERFLOW,
	MEMERR_USER_OVERREAD,
	// User writing errors
	MEMERR_USER_STACK_OVERFLOW,
	MEMERR_USER_HEAP_OVERFLOW,
	MEMERR_USER_TEXT_WRITE,
	MEMERR_USER_CONST_WRITE,
	MEMERR_USER_SECT_WRITE, // Writing to other sections not in user space

	// User reading errors
	MEMERR_USER_SECT_READ // Reading from other sections that is not system libraries
} memerr_t;


memerr_t validKIMemAddr(uint32_t addr);
memerr_t validUIMemAddr(uint32_t addr);

char memReadByte(uint32_t addr, memerr_t* memerr);
short memReadShort(uint32_t addr, memerr_t* memerr);
int memReadInt(uint32_t addr, memerr_t* memerr);

memerr_t memWriteByte(uint32_t addr, char data);
memerr_t memWriteShort(uint32_t addr, short data);
memerr_t memWriteInt(uint32_t addr, int data);


#endif