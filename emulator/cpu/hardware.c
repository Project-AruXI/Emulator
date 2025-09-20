#include "hardware.h"
#include "core.h"
#include "diagnostics.h"

extern core_t core;

void alu() {
	uint32_t res = 0xFAEDDEAF;

	uint32_t vala = ExecuteCtx.aluVala;
	uint32_t valb = ExecuteCtx.aluValb;

	switch (DecodeCtx.aluop)	{
		case ALU_PLUS:
			res = vala + valb;
			break;
		case ALU_MINUS:
			res = vala - valb;
			break;
		case ALU_MUL:
			res = vala * valb;
			break;
		case ALU_DIV:
			res = vala / valb;
			break;
		case ALU_OR:
			res = vala | valb;
			break;
		case ALU_XOR:
			res = vala ^ valb;
			break;
		case ALU_AND:
			res = vala & valb;
			break;
		case ALU_INV:
			res = ~vala;
			break;
		case ALU_LSL:
			res = vala << valb;
			break;
		case ALU_LSR:
			res = vala >> valb;
			break;
		case ALU_ASR:
			res = (int32_t)(vala >> valb);
			break;
		case ALU_PASS:
			res = 0;
			break;
		default:
			break;
	}

	ExecuteCtx.alures = res;

	if (DecodeCtx.setCC) {
		dLog(D_NONE, DSEV_INFO, "Setting condition");
		bool C = false;
		bool O = false;
		bool N = (res & 0x80000000) == 0;
		bool Z = res == 0;


		C = res < vala || res < valb;

		int32_t signedVala = (int32_t) vala;
		int32_t signedValb = (int32_t) valb;
		int32_t signedRes = signedVala + signedValb;

		// Check overflow for substraction
		if (DecodeCtx.aluop == ALU_MINUS) {
			signedRes = signedVala - signedValb;

			if (signedVala > 0 && signedValb < 0 && signedRes > 0) O = true;
			if (vala >= valb) C = true;
			else if (signedVala < 0 && signedValb > 0 && signedRes < 0) O = true;
		} else { // for addition
			if (signedVala >= 0 && signedValb >= 0 && signedRes <= 0) O = true;
			else if (signedVala < 0 && signedValb < 0 && signedRes >= 0) O = true;
		}

		core.CSTR = (core.CSTR & ~0xF) | (SET_CONDS(C,O,N,Z) & 0xF);
		dLog(D_NONE, DSEV_INFO, "Flags: 0x%x", SET_CONDS(C,O,N,Z));
	}
}

void fpu() {}

void vcu() {}

void regfile(bool write) {
	const int COMMIT_PRIV = 0b1<<15;
	static uint32_t setCSTR;

	if ((setCSTR >> 15) == 0b1) {
		// Commit CSTR, ignoring last bit
		core.CSTR = setCSTR & ~COMMIT_PRIV;
		setCSTR = 0x00000000; // reset
	}


	if (write) {	
		// Do not allow write to X30
		if (DecodeCtx.rd == 30) return;

		// Special registers
		if (FetchCtx.opcode == OP_LDIR) {
			// MOVE THIS TO READ FROM REGFILE
			// THIS IS HERE TO EVADE THE ALU FROM RETURNING 0 FOR VALOUT
			MemoryCtx.valout = core.IR;
			dLog(D_NONE, DSEV_INFO, "regfile(special)::Writing from IR");
		} else if (FetchCtx.opcode == OP_LDCSTR) {
			MemoryCtx.valout = core.CSTR;
			dLog(D_NONE, DSEV_INFO, "regfile(special)::Writing from CSTR");
		} else if (FetchCtx.opcode == OP_MVCSTR) {
			// core.CSTR = MemoryCtx.valout;
			// dLog(D_NONE, DSEV_INFO, "regfile(special)::Writing 0x%x to CSTR", MemoryCtx.valout);
			// Special attention!!!
			// Delay the writing of CSTR mainly for the PRIV bit so the next kernel instruction is allowed to run
			// Considering only a few bits of the CSTR are used, the msb can be used to indicate whether to write it or not
			// Basically a boolean "writebackCSTR?"
			// This writeback is done regardless if there is a normal writeback or not in order to commit to the next instruction
			// The first step is on MVCSTR cycle, indicating to commit and set it
			setCSTR = MemoryCtx.valout | COMMIT_PRIV;
			return;
		} else if (FetchCtx.opcode == OP_RESR) {
			MemoryCtx.valout = core.ESR;
			dLog(D_NONE, DSEV_INFO, "regfile(special)::Writing from ESR");
		}

		if (DecodeCtx.rd == 31) {
			core.SP = MemoryCtx.valout;
		} else if (DecodeCtx.rd != 30) {
			core.GPR[DecodeCtx.rd] = MemoryCtx.valout;
		}
		dLog(D_NONE, DSEV_INFO, "regfile::Writing 0x%x to register %d", MemoryCtx.valout, DecodeCtx.rd);
		return;
	}

	if (DecodeCtx.rs == 31) DecodeCtx.vala = core.SP;
	else DecodeCtx.vala = core.GPR[DecodeCtx.rs];

	if (DecodeCtx.rr == 31) DecodeCtx.valb = core.SP;
	else DecodeCtx.valb = core.GPR[DecodeCtx.rr];

	// M types are mainly the ones to use VALEX since they support three registers
	// VALEX used for the destination (for LD*)/source (for STR*)
	// RS is always present as the base
	// RR may be present as index
	if (DecodeCtx.rd == 31) DecodeCtx.valex = core.SP;
	else DecodeCtx.valex = core.GPR[DecodeCtx.rd];

	// Special register access
	// if (FetchCtx.opcode == OP_LDIR) {

	// }

	dLog(D_NONE, DSEV_INFO, "regfile::Reading 0x%x from rs %d, 0x%x from rr %d, and 0x%x from rd %d", 
			DecodeCtx.vala, DecodeCtx.rs, DecodeCtx.valb, DecodeCtx.rr, DecodeCtx.valex, DecodeCtx.rd);
}

void imem(uint32_t addr, uint32_t* ival, memerr_t* imemErr) {
	if (GET_PRIV(core.CSTR) == PRIVILEGE_KERNEL) *imemErr = validKIMemAddr(addr); // Kernel mode
	else *imemErr = validUIMemAddr(addr);

	if (*imemErr != MEMERR_NONE) return;

	*ival = (uint32_t) memReadInt(addr, imemErr);
}

void dmem(uint32_t addr, uint32_t* rval, uint32_t* wval, memerr_t* imemErr) {
	if (DecodeCtx.memSize == 0) return;

	// Both cannot be null at the same time
	if (!rval && !wval) { *imemErr = MEMERR_INTERNAL; return; }

	int (*memRead)(uint32_t, memerr_t*);
	memerr_t (*memWrite)(uint32_t, int);

	switch (DecodeCtx.memSize)	{
		case 1:
			memRead = &memReadByte;
			memWrite = &memWriteByte;
			break;
		case 2:
			memRead = &memReadShort;
			memWrite = &memWriteShort;
			break;
		case 4:
			memRead = &memReadInt;
			memWrite = &memWriteInt;
			break;
	}

	if (wval) *imemErr = memWrite(addr, *wval);
	else *rval = (uint32_t) memRead(addr, imemErr);
}