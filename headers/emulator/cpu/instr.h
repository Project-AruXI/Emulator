#ifndef _INSTR_H_
#define _INSTR_H_

#include <stdbool.h>

typedef enum opcode {
	OP_NOP,

	OP_MUL,
	OP_SMUL,
	OP_DIV,
	OP_SDIV,
	OP_ADD,
	OP_ADDS,
	OP_SUB,
	OP_SUBS,
	OP_OR,
	OP_AND,
	OP_XOR,
	OP_NOT,
	OP_LSL,
	OP_LSR,
	OP_ASR,
	OP_CMP,
	OP_MV,
	OP_MVN,

	OP_SXB,
	OP_SXH,
	OP_UXB,
	OP_UXH,

	OP_LD,
	OP_LDB,
	OP_LDBS,
	OP_LDBZ,
	OP_LDH,
	OP_LDHS,
	OP_LDHZ,
	OP_STR,
	OP_STRB,
	OP_STRH,

	OP_UB,
	OP_UBR,
	OP_B,
	OP_CALL,
	OP_RET,

	OP_ADDF,
	OP_SUBF,
	OP_MULF,
	OP_DIVF,
	OP_LDF,
	OP_STRF,
	OP_MVF,

	OP_SYS, // Applies to all S-types
	OP_SYSCALL,
	OP_HLT,
	OP_SI,
	OP_DI,
	OP_ERET,
	OP_LDIR,
	OP_MVCSTR,
	OP_LDCSTR,
	OP_RESR,

	OP_ERROR = -1
} opcode_t;

typedef enum cond {
	COND_EQ,
	COND_NE,
	COND_OV,
	COND_NV,
	COND_MI,
	COND_PZ,
	COND_CC,
	COND_CS,
	COND_GT,
	COND_GE,
	COND_LT,
	COND_LE
} cond_t;

typedef enum aluOP {
	ALU_PLUS,
	ALU_MINUS,
	ALU_MUL,
	ALU_DIV,
	ALU_OR,
	ALU_XOR,
	ALU_AND,
	ALU_LSL,
	ALU_LSR,
	ALU_ASR,
	ALU_INV,
	ALU_PASS
} aluop_t;

static char* ALUOP_STR[] = {
	"ALU_PLUS",
	"ALU_MINUS",
	"ALU_MUL",
	"ALU_DIV",
	"ALU_OR",
	"ALU_XOR",
	"ALU_AND",
	"ALU_LSL",
	"ALU_LSR",
	"ALU_ASR",
	"ALU_INV",
	"ALU_PASS"
};

typedef enum {
	NO_TYPE = -1,
	I_TYPE,
	R_TYPE,
	M_TYPE,
	BI_TYPE,
	BU_TYPE,
	BC_TYPE,
	S_TYPE,
	F_TYPE
} itype_t;

typedef struct InstructionContext {
	struct {
		uint32_t instrbits;
		opcode_t opcode;
	} fetchCtx;

	struct {
		itype_t iType;
		int32_t imm;
		uint32_t rd, rs, rr;
		aluop_t aluop;
		uint32_t vala, valb, valex;
		bool setCC;
		int memSize;
		bool regwrite; // Write to register
		bool memwrite; // Write the memory contents to register (ld*)
	} decodeCtx;

	struct {
		uint32_t aluVala; // ALU input
		uint32_t aluValb; // ALU input
		uint32_t alures; // ALU output
		bool cond;

		float fpuVala; // FPU input
		float fpuValb; // FPU input
		float fpures; // FPU output


	} executeCtx;

	struct {
		uint32_t valmem; // Data to write
		bool write;
		uint32_t valout; // Data read
	} memoryCtx;
} InstrCtx;

#define FetchCtx core.uarch.fetchCtx
#define DecodeCtx core.uarch.decodeCtx
#define ExecuteCtx core.uarch.executeCtx
#define MemoryCtx core.uarch.memoryCtx

#define u32bitextract(bits, start, width) ((bits>>start) & ((1<<width)-1))
#define s32bitextract(bits, start, width) ( (int32_t)(( (bits>>start) & ((1<<width)-1) ) << (32-width)) ) >> (32-width)

#endif