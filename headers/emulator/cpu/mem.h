#ifndef _MEM_H_
#define _MEM_H_

#include <stdint.h>

#define KERN_START 0xA0080000 
#define KERN_DATA 0xA0080000
#define KERN_TEXT 0xB8080000
#define KERN_HEAP 0xD0080000
#define KERN_STACK 0xF0080000
#define KERN_STACK_LIMIT 0xFFFFFFFF

#define USER_START 0x20040000
#define USER_BSS 0x20040000
#define USER_CONST 0x20080000
#define USER_DATA 0x20090000
#define USER_TEXT 0x20190000
#define USER_HEAP 0x20990000
#define USER_HEAP_LIMIT 0x6098FFFF
#define USER_STACK 0x60990800
#define USER_STACK_LIMIT 0x709907FF

#define SYS_LIB 0x00080000
#define SYS_LIB_LIMIT 0x1007FFFF

#define EVT_START 0x00040000
#define EVT_LIMIT 0x0007FFFF


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