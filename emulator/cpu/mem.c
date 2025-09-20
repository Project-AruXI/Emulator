#include <stdbool.h>

#include "mem.h"
#include "core.h"
#include "diagnostics.h"


extern core_t core;
extern uint8_t* emMem;

memerr_t validKIMemAddr(uint32_t addr) {
	bool inTextSect = (addr < KERN_HEAP && addr >= KERN_TEXT);
	dDebug(DB_TRACE, "addr 0x%x in text? %d", addr, inTextSect);
	bool inSyslib = (addr <= SYS_LIB_LIMIT && addr >= SYS_LIB);
	dDebug(DB_TRACE, "addr 0x%x in syslib? %d", addr, inSyslib);
	bool inEvt = (addr <= EVT_LIMIT && addr >= EVT_START);
	dDebug(DB_TRACE, "addr 0x%x in evt? %d", addr, inEvt);

	// Even though SECT_READ allows reading in heap/stack/data, this is for instruction memory
	if (!inTextSect && !inSyslib && !inEvt) return MEMERR_KERN_SECT_READ;
	else return MEMERR_NONE;
}

memerr_t validUIMemAddr(uint32_t addr) {
	bool inTextSect = (addr < USER_HEAP && addr >= USER_TEXT);
	bool inSyslib = (addr <= SYS_LIB_LIMIT && SYS_LIB);

	if (!inTextSect && !inSyslib) return MEMERR_USER_SECT_READ;
	else return MEMERR_NONE;
}


uint32_t _memRead(uint32_t addr, unsigned width, memerr_t* memerr) {
	// Check that the given address is in a permissible starting point
	// This does not check whether the memory reading crosses boundaries
	if (GET_PRIV(core.CSTR) == PRIVILEGE_KERNEL) {
		// Kernel checks
		bool validRead = (addr >= SYS_LIB && addr <= SYS_LIB_LIMIT) || (addr >= KERN_START && addr <= KERN_STACK_LIMIT) || 
				(addr >= EVT_START && addr <= EVT_LIMIT);

		if (!validRead) { *memerr = MEMERR_KERN_SECT_READ; return 0; }
	} else {
		// User checks
		bool invalidProcRead = (addr > USER_HEAP_LIMIT && addr < USER_STACK) || (addr > USER_STACK_LIMIT);
		bool invalidSectRead = (addr < SYS_LIB) || (addr > SYS_LIB_LIMIT && addr < USER_START);

		if (invalidProcRead || invalidSectRead) { *memerr = MEMERR_USER_SECT_READ; return 0; }
	}

	dDebug(DB_TRACE, "Reading from 0x%x with width %d", addr, width);
	uint32_t val = 0x00000000;

	val = *(emMem + addr+3);
	dDebug(DB_TRACE, "Byte 0x%x", val);
	for (int i = width-2; i >= 0; i--) {
		// Check that addr+i is not crossing boundaries
		uint32_t newaddr = addr+i;
		dDebug(DB_TRACE, "Reading from new address 0x%x", newaddr);
		if (GET_PRIV(core.CSTR) == PRIVILEGE_KERNEL) {
			// Began in sys libs, crossing over to reserved or EVT
			bool overreadSyslib = (newaddr == SYS_LIB_LIMIT+1) || (newaddr == SYS_LIB-1);
			// Began in kernel space, crossing over to IVT (wrap around) or buffer
			bool overreadKernel = (newaddr == 0x0) || (newaddr == KERN_START-1);

			if (overreadSyslib || overreadKernel) { *memerr = MEMERR_KERN_OVERREAD; return val; }
		} else {
			// Began in heap, crossing over to safeguard
			bool overreadHeap = (newaddr == USER_HEAP_LIMIT+1);
			// Began in stack, crossing over to safeguard or to unused
			bool overreadStack = (newaddr == USER_STACK-1) || (newaddr == USER_STACK_LIMIT+1);
			// Began in bss, crossing over to buffer
			bool overreadBss = (newaddr == USER_START-1);
			// Began in sys libs, crossing over to reserved or EVT
			bool overreadSyslib = (newaddr == SYS_LIB_LIMIT+1) || (newaddr == SYS_LIB-1);

			if (overreadHeap || overreadStack || overreadBss || overreadSyslib) { *memerr = MEMERR_USER_OVERREAD; return val; }
		}
		val = (val<<8) + (*(emMem + (addr+i)));
		dDebug(DB_TRACE, "Byte 0x%x -> 0x%x", *(emMem+newaddr), val);
	}
	dDebug(DB_TRACE, "Returning 0x%x", val);
	return val;
}

memerr_t _memWrite(uint32_t addr, uint32_t data, unsigned width) {
	// Check that the given address is in a permissible starting point
	// This does not check whether the memory writing crosses boundaries
	if (GET_PRIV(core.CSTR) == PRIVILEGE_KERNEL) {
		// Kernel checks
		bool invalidWrite = (addr < KERN_DATA); // writing outside kernel space
		if (invalidWrite) return MEMERR_KERN_SECT_WRITE;

		bool invalidTextWrite = (addr >= KERN_TEXT && addr < KERN_HEAP); // writing in text section
		if (invalidTextWrite) return MEMERR_KERN_TEXT_WRITE;
	} else {
		// User checks
		bool invalidWrite = (addr < USER_BSS) || (addr > USER_HEAP_LIMIT && addr < USER_STACK) || (addr > USER_STACK_LIMIT);
		if (invalidWrite) return MEMERR_USER_SECT_WRITE;

		bool invalidTextWrite = (addr >= USER_TEXT && addr < USER_HEAP);
		if (invalidTextWrite) return MEMERR_USER_TEXT_WRITE;

		bool invalidConstWrite = (addr >= USER_CONST && addr < USER_DATA);
		if (invalidConstWrite) return MEMERR_USER_CONST_WRITE;
	}

	dDebug(DB_TRACE, "Writing 0x%x to 0x%x with width %d", data, addr, width);
	uint8_t* _data = (uint8_t*)&data;

	*(emMem + addr) = _data[0];
	for (int i = 1; i < width; i++) {
		// Check that addr+i is not crossing boundaries
		uint32_t newaddr = addr+i;
		if (GET_PRIV(core.CSTR) == PRIVILEGE_KERNEL) {
			// Began in data, crossing over to text (and from heap), to buffer, or wrap around to IVT from stack
			bool overwrite = (newaddr == KERN_TEXT) || (newaddr == KERN_HEAP-1) || (newaddr == KERN_START-1) || (newaddr == 0x0);
			if (overwrite) return MEMERR_KERN_OVERFLOW;

			// Began in heap, crossing over to stack
			bool overwriteStack = (newaddr == KERN_STACK);
			if (overwriteStack) return MEMERR_KERN_HEAP_OVERFLOW;

			// Began in stack, crossing over to heap
			bool overwriteHeap = (newaddr == KERN_STACK-1);
			if (overwriteHeap) return MEMERR_KERN_STACK_OVERFLOW;
		} else {
			// Began in bss, crossing over to const or to buffer
			bool overwriteConstReserved = (newaddr == USER_CONST) || (newaddr == USER_BSS-1);
			// Began in data, crossing over to text or to const
			bool overwriteTextConst = (newaddr == USER_TEXT) || (newaddr == USER_DATA-1);
			// Began in heap, crossing over to text
			bool overwriteText = (newaddr == USER_HEAP-1);
			// Began in stack, crossing over to buffer
			bool overwriteBuffer = (newaddr == USER_STACK_LIMIT+1);

			if (overwriteConstReserved || overwriteTextConst || overwriteText || overwriteBuffer) return MEMERR_USER_OVERFLOW;

			// Began in heap, crossing over to safeguard
			bool overwriteSafe = (newaddr == USER_HEAP_LIMIT+1);
			if (overwriteSafe) return MEMERR_USER_HEAP_OVERFLOW;

			// Began in stack, crossing over to safeguard
			overwriteSafe = (newaddr == USER_STACK-1);
			if (overwriteSafe) return MEMERR_USER_STACK_OVERFLOW;
		}


		*(emMem + (addr+i)) = _data[i];
	}

	return MEMERR_NONE;
}

char memReadByte(uint32_t addr, memerr_t* memerr) { return (char) _memRead(addr, 1, memerr); }
short memReadShort(uint32_t addr, memerr_t* memerr) { return (short) _memRead(addr, 2, memerr); }
int memReadInt(uint32_t addr, memerr_t* memerr) { return (int) _memRead(addr, 4, memerr); }

memerr_t memWriteByte(uint32_t addr, char data) {return _memWrite(addr, (uint32_t) data, 1); }
memerr_t memWriteShort(uint32_t addr, short data) {return _memWrite(addr, (uint32_t) data, 2); }
memerr_t memWriteInt(uint32_t addr, int data) {return _memWrite(addr, (uint32_t) data, 4); }