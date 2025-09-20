#ifndef _CORE_H_
#define _CORE_H_

#include <stdint.h>

#include "instr.h"


typedef enum status {
	STAT_HLT,
	STAT_RUNNING,
	STAT_EXCP,
	STAT_IO
} status_t;

typedef struct __attribute__((packed)) VectorRegister {
	union {
		uint64_t _v64[8];
		uint32_t _v32[16];
	};
} vec_reg_t;

typedef enum ExceptionType {
	EXCP_SYSCALL = 0b00,
	EXCP_DATA_ABORT = 0b01,
	EXCP_FETCH_ABORT = 0b10
} excp_t;

typedef enum ExceptionNumber {
	EXCPN_SYSCALL_READ = 0b0000,
	EXCPN_SYSCALL_WRITE = 0b0001,
	EXCPN_SYSCALL_EXIT = 0b0010,


	EXCPN_ABORT_ACCESS = 0b1100,
	EXCPN_ABORT_INSTR = 0b1101,
	EXCPN_ABORT_PRIV = 0b1110
} excp_n;

typedef struct __attribute__((packed)) UserProcessThreadState {
	uint8_t tid;
} PTS;

typedef struct __attribute__((packed)) UserProcessState {
	uint8_t pid;
	uint8_t threadc;
	// PTS** threadStates; // Since normal pointers are 8 bytes but pointers in the Aru architecture are 4, need to replace with uint32
	uint32_t threadStates;
	uint32_t sp;
	uint32_t ir;
	uint16_t cstr;
	uint16_t esr;
	uint32_t gpr[25];
	float fpr[16];
	vec_reg_t vr[6];
	excp_t excpType;
} PS;

typedef struct Core {
	uint32_t GPR[31];
	uint32_t IR;
	uint32_t SP;
	float FPR[16];
	vec_reg_t VR[6];
	uint16_t CSTR;
	uint16_t ESR;

	InstrCtx uarch;
	status_t status;
} core_t;

/**
	| 15 | 14 | 13 | 12 | 11 | 10 | 9 | 8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
	| -  | A  | -  | -  | -  | -  | L | - | - | I | - | - | C | O | N | Z |
	A: Whether the AVExt is enabled
	L: The privilege level; 0 for user; 1 for kernel
	I: If interrupts are enabled
	C: Carry flag
	O: Overflow flag
	N: Negtive flag
	Z: Zero flag
*/

#define SET_CONDS(C,O,N,Z) ((C<<3)|(O<<2)|(N<<1)|(Z<<0))
#define SET_INT(bits) (bits | (1<<6))
#define CLR_INT(bits) (bits & ~(1<<6))
#define SET_PRIV(bits) (bits | (1<<9))
#define CLR_PRIV(bits) (bits & ~(1<<9))
#define SET_AVE(bits) (bits | (1<<14))

#define GET_C(flags) ((flags>>3) & 0b1)
#define GET_O(flags) ((flags>>2) & 0b1)
#define GET_N(flags) ((flags>>1) & 0b1)
#define GET_Z(flags) ((flags>>0) & 0b1)
#define GET_INT(flags) ((flags>>6) & 0b1)
#define GET_PRIV(flags) ((flags>>9) & 0b1)
#define GET_AVE(flags) ((flags>>14) & 0b1)

#define INTERRUPTS_ENABLE 0b1
#define PRIVILEGE_KERNEL 0b1
#define AVEXT_ENABLE 0b1


void initCore();
void* runCore(void*);

#endif