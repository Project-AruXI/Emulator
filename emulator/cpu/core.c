#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <pthread.h>
// #include <unistd.h>

#include "core.h"
#include "mem.h"
#include "hardware.h"
#include "emSignal.h"
#include "diagnostics.h"


extern core_t core;
extern opcode_t imap[];
extern char* istrmap[];
extern SigMem* sigMem;
extern uint8_t* emMem;
PS* userPS;

extern pthread_mutex_t idleLock;
extern pthread_cond_t idleCond;
extern volatile bool IDLE;


static void fault() {
	sigMem->metadata.signalType = UNIVERSAL_SIG;
	int set = setFaultSignal(GET_SIGNAL(sigMem->signals, UNIVERSAL_SIG));
	if (set != -1) {
		fflush(stderr);
		fflush(stdout);
		flushDebug();
		kill(sigMem->metadata.shellPID, SIGUSR1);
		raise(SIGUSR1);
	} else dLog(D_NONE, DSEV_WARN, "Was not able to set fault signal!");
}

static void exception(uint16_t excpNum) {
	dLog(D_NONE, DSEV_INFO, "Exception 0x%x!", excpNum);

	uint32_t __psPtr = KERN_DATA + 0x4; // The virtual address where the pointer to PS is stored
	uint8_t* _psPtr = emMem + __psPtr; // The real address where the pointer to PS is stored
	uint32_t psPtr = *((uint32_t*)_psPtr); // The virtual address of PS
	userPS = (PS*) (emMem + psPtr);

	// CPU saves IR, save to PS->IR
	// Note that IR saved is actually at the next instruction due to core.IR++ right after fetching
	// and all exceptions occur after
	// This is helpful for syscalls where it needs to return to the next instruction (after `syscall`)
	// However, should it be an abort, the dump IR is the next (+4)
	// or even a very different one if the abort occurs after `setIR`
	userPS->ir = core.IR;

	core.IR = 0x00040000;

	// Place exception number
	core.ESR = excpNum;

	// Change mode to kernel
	core.CSTR = SET_PRIV(core.CSTR);

	core.status = STAT_EXCP;
}

static void fetch() {
	// Reset state for new cycle if exception
	if (core.status == STAT_EXCP) core.status = STAT_RUNNING;

	// Instructions cannot be 0
	FetchCtx.instrbits = 0x00000000;

	memerr_t err;
	dLog(D_NONE, DSEV_INFO, "fetch::Fetching from 0x%x...", core.IR);
	imem(core.IR, &FetchCtx.instrbits, &err);

	core.IR += 4;
	dDebug(DB_DETAIL, "Fetched from 0x%x, automatically increment to 0x%x", core.IR-4, core.IR);

	if (err != MEMERR_NONE) {
		if (GET_PRIV(core.CSTR) != PRIVILEGE_KERNEL) {
			// user code attempted to access elsewhere, run EVT
			switch (err) {
				case MEMERR_USER_SECT_READ:
					dLog(D_NONE, DSEV_WARN, "User code attempted to access outside of its Process Address Space or System Libraries");
					break;
				case MEMERR_USER_OVERREAD:
					dLog(D_NONE, DSEV_WARN, "User code attempted to overread from an allowed address");
					break;
				default:
					break;
			}
			exception(EXCPN_ABORT_ACCESS);
		} else {
			dLog(D_NONE, DSEV_WARN, "Kernel code attempted to access outside of permissible range!");
			fault();
		}
	}

	dLog(D_NONE, DSEV_INFO, "fetch::Got 0x%x", FetchCtx.instrbits);
}


void extractImm() {
	uint16_t imm14 = (uint16_t) u32bitextract(FetchCtx.instrbits, 10, 14);
	int32_t simm24 = (int32_t) s32bitextract(FetchCtx.instrbits, 0, 24);
	int32_t simm19 = (int32_t) s32bitextract(FetchCtx.instrbits, 5, 19);
	int16_t simm9 = s32bitextract(FetchCtx.instrbits, 15, 9);

	opcode_t opcode = FetchCtx.opcode;
	itype_t type = NO_TYPE;
	int32_t imm = 0x0;

	if (opcode >= OP_NOP && opcode <= OP_CMP) {
		if (((((FetchCtx.instrbits >> 24) & 0xff) & 0b1) == 0b0) && (opcode < OP_MUL || opcode > OP_SDIV)) {
			imm = imm14; // I-types
			type = I_TYPE;
		} else type = R_TYPE;
	} else if (opcode >= OP_LD && opcode <= OP_STRH) {
		imm = simm9; // M-types
		type = M_TYPE;
	} else if (opcode == OP_UB || opcode == OP_CALL) {
		imm = simm24; // Bi-types
		type = BI_TYPE;
	} else if (opcode == OP_B) {
		imm = simm19; // Bc-types
		type = BC_TYPE;
	//} else if (DecodeCtx.iType == S_TYPE) {
		// S-types don't have an imm as of now
		// Since iType is reset, keep it
		// type = S_TYPE;
		// Orrr just have this occur before checking for S types
	} else if (opcode == OP_UBR) type = BU_TYPE;

	DecodeCtx.imm = imm;
	DecodeCtx.iType = type;
}

void extractRegs() {
	uint8_t _rd = u32bitextract(FetchCtx.instrbits, 0, 5);
	uint8_t _rsIS = u32bitextract(FetchCtx.instrbits, 5, 5);
	uint8_t _rsR = u32bitextract(FetchCtx.instrbits, 10, 5);
	uint8_t _rr = u32bitextract(FetchCtx.instrbits, 5, 5);

	uint8_t rd, rs, rr;

	itype_t type = DecodeCtx.iType;
	opcode_t opcode = FetchCtx.opcode;

	if (opcode == OP_UBR || opcode == OP_RET) {
		rs = _rd; // Bu-Type
		DecodeCtx.iType = BU_TYPE;
	} else if (type == I_TYPE) {
		rd = _rd;
		rs = _rsIS;
	} else if (type == R_TYPE || type == M_TYPE) {
		rd = _rd;
		rs = _rsR;
		rr = _rr;

		if (type == M_TYPE) {
			// There is either `reg, [x]`, `reg, [x, imm]`, or `reg, [x], y`
			// Meaning addition will either be in between x and imm or x and y
			// rd in this context means the register containing the stuff to store
			// rs meaning the base register
			// rr meaning the index register (gets added to base)
		}
	} else if (type == S_TYPE) {
		if (opcode == OP_MVCSTR) rs = _rsIS;
		else if (opcode == OP_LDIR || opcode == OP_LDCSTR || opcode == OP_RESR) rd = _rd;
	}

	DecodeCtx.rd = rd;
	DecodeCtx.rs = rs;
	DecodeCtx.rr = rr;
}

void decideALUOp() {
	aluop_t aluop = ALU_PASS;
	switch (FetchCtx.opcode)	{
		// MV, MVN, CMP, and NOP do not appear as they are aliased

		case OP_ADD: case OP_ADDS: case OP_LD: case OP_LDB:
		case OP_LDBS: case OP_LDBZ: case OP_LDH: case OP_LDHS:
		case OP_LDHZ: case OP_STR: case OP_STRB: case OP_STRH:
		case OP_MV: // MV includes both MVI and MVR, which MVR uses OR
		case OP_CALL:
			aluop = ALU_PLUS;
			break;
		case OP_SUB: case OP_SUBS: case OP_MVN: case OP_CMP:
			aluop = ALU_MINUS;
			break;
		case OP_OR:
			aluop = ALU_OR;
			break;
		case OP_AND:
			aluop = ALU_AND;
			break;
		case OP_XOR:
			aluop = ALU_XOR;
			break;
		case OP_NOT:
			aluop = ALU_INV;
			break;
		case OP_LSL:
			aluop = ALU_LSL;
			break;
		case OP_LSR:
			aluop = ALU_LSR;
			break;
		case OP_ASR:
			aluop = ALU_ASR;
			break;
		case OP_MUL: case OP_SMUL:
			aluop = ALU_MUL;
			break;
		case OP_DIV: case OP_SDIV:
			aluop = ALU_DIV;
			break;
		default:
			break;
	}

	DecodeCtx.aluop = aluop;
}

static bool checkCondition() {
	cond_t cond = (cond_t) u32bitextract(FetchCtx.instrbits, 0, 4);

	dLog(D_NONE, DSEV_INFO, "Checking condition 0x%x", cond);
	dLog(D_NONE, DSEV_INFO, "Status 0x%x", core.CSTR);

	switch (cond)	{
		case COND_EQ: return GET_Z(core.CSTR) == 1;
		case COND_NE: return GET_Z(core.CSTR) == 0;
		case COND_OV: return GET_O(core.CSTR) == 1;
		case COND_NV: return GET_O(core.CSTR) == 0;
		case COND_MI: return GET_N(core.CSTR) == 1;
		case COND_PZ: return GET_N(core.CSTR) == 0;
		case COND_CC: return GET_C(core.CSTR) == 0;
		case COND_CS: return GET_C(core.CSTR) == 1;
		case COND_GT: return GET_N(core.CSTR) == GET_O(core.CSTR) && GET_Z(core.CSTR) == 0;
		case COND_GE: return GET_N(core.CSTR) == GET_O(core.CSTR);
		case COND_LT: return GET_N(core.CSTR) != GET_O(core.CSTR);
		case COND_LE: return GET_N(core.CSTR) != GET_O(core.CSTR) || GET_Z(core.CSTR) != 0;
	}

	return false;
}

static void nextIR() {
	// UB only needs imm and IR, all execution happens here
	// UBR/RET only needs to have value of xd from regfile, all execution happens here
	// BCOND needs imm and condval from prev execution, all execution happens here
	// CALL only needs imm and IR; writeback for lr is later
	// 	If before ALU(), place IR contents to vala and 4 to valb
	// 	Aka have CALL intercept since it doesn't have its data in encoding

	if (DecodeCtx.iType == BI_TYPE) {
		if (FetchCtx.opcode == OP_CALL) {
			// Intercept for ALU to do LR := IR + 4
			
			DecodeCtx.rd = 28; // LR
			ExecuteCtx.aluVala = core.IR-4; // undo the +4
			ExecuteCtx.aluValb = 0x4;

			dLog(D_NONE, DSEV_INFO, "execute::call val a: 0x%x; val b: 0x%x; rd: %d", ExecuteCtx.aluVala, ExecuteCtx.aluValb, DecodeCtx.rd);
		}
		// IR += 4 was done automatically after imem, reverse it
		core.IR = (core.IR-4) + (((int32_t)(((DecodeCtx.imm & 0xffffff) << 2) << 9)) >> 9);
		return;
	}

	if (DecodeCtx.iType == BU_TYPE) {
		// vala comes from rs
		core.IR = DecodeCtx.vala;
		return;
	}

	// else BCOND
	if (ExecuteCtx.cond) {
		dLog(D_NONE, DSEV_INFO, "Condition true. Branching");
		core.IR = (core.IR-4) + ((DecodeCtx.imm & 0x7ffff) << 2);
	}

	if (FetchCtx.opcode == OP_ERET) {
		dLog(D_NONE, DSEV_INFO, "Returning from exception");
		core.IR = userPS->ir;
	}
}

static void decode() {
	if (core.status == STAT_EXCP) return;

	uint8_t opcode = (FetchCtx.instrbits >> 24) & 0xff;
	opcode_t code = imap[opcode];
	FetchCtx.opcode = code;

	dLog(D_NONE, DSEV_INFO, "decode::Opcode: 0x%x; code %d -> %s", opcode, code, (code != OP_ERROR) ? istrmap[code] : "OP_ERROR");

	// Invalid instruction
	if (code == OP_ERROR) {
		dLog(D_NONE, DSEV_WARN, "Invalid instruction: 0x%x!", opcode);
		if (GET_PRIV(core.CSTR) == PRIVILEGE_KERNEL) { // Kill for kernel code
			fault();
		} else { // EVT for user code
			exception(EXCPN_ABORT_INSTR);
		}
	}

	if (code == OP_ADDS || code == OP_SUBS || code == OP_CMP) DecodeCtx.setCC = true;
	else DecodeCtx.setCC = false;

	switch (code) {
		case OP_LDB: case OP_LDBS: case OP_LDBZ: case OP_STRB:
			DecodeCtx.memSize = 1;
			break;
		case OP_LDH: case OP_LDHS: case OP_LDHZ: case OP_STRH:
			DecodeCtx.memSize = 2;
			break;
		case OP_LD: case OP_STR:
			DecodeCtx.memSize = 4;
			break;
		default:
			DecodeCtx.memSize = 0;
			break;
	}

	extractImm();
	dLog(D_NONE, DSEV_INFO, "decode::Imm: 0x%x", DecodeCtx.imm);

	// Subdivide for S-types
	if (code == OP_SYS) {
		DecodeCtx.iType = S_TYPE;
		// get subopcode
		uint8_t subopcode = u32bitextract(FetchCtx.instrbits, 19, 5);
		opcode_t subcode = OP_ERROR;
		switch (subopcode)	{
			case 0b00010: subcode = OP_SYSCALL; break;
			case 0b00110: subcode = OP_HLT; break;
			case 0b01010: subcode = OP_SI; break;
			case 0b01110: subcode = OP_DI; break;
			case 0b10010: subcode = OP_ERET; break;
			case 0b10110: subcode = OP_LDIR; break;
			case 0b11010: subcode = OP_MVCSTR; break;
			case 0b11110: subcode = OP_LDCSTR; break;
			case 0b11111: subcode = OP_RESR; break;
			default: break;
		}

		dLog(D_NONE, DSEV_INFO, "OP_SYS -> %s (opcode %d)", (subcode != OP_ERROR) ? istrmap[subcode] : "OP_ERROR", subcode);

		if (subcode == OP_ERROR) {
			dLog(D_NONE, DSEV_WARN, "Invalid instruction: 0x%x!", subcode);
			if (GET_PRIV(core.CSTR) == PRIVILEGE_KERNEL) { // Kill for kernel code
				fault();
			} else { // EVT for user code
				exception(EXCPN_ABORT_INSTR);
			}
		}

		if (subcode != OP_SYSCALL) {
			// Syscall is the only S-type that is unprivileged (for now??), the rest are privileged
			if (GET_PRIV(core.CSTR) != PRIVILEGE_KERNEL) {
				dLog(D_NONE, DSEV_WARN, "Used privileged instruction 0x%x!", subcode);
				exception(EXCPN_ABORT_PRIV);
			}
		}

		FetchCtx.opcode = subcode;
	}

	extractRegs();
	dLog(D_NONE, DSEV_INFO, "decode::Rd: %d; Rs: %d; Rr: %d", DecodeCtx.rd, DecodeCtx.rs, DecodeCtx.rr);

	DecodeCtx.regwrite = false;
	DecodeCtx.memwrite = false;
	ExecuteCtx.cond = false;

	if (DecodeCtx.iType == I_TYPE || DecodeCtx.iType == R_TYPE || (FetchCtx.opcode >= OP_LD && FetchCtx.opcode <= OP_LDHZ) || 
		FetchCtx.opcode == OP_CALL || (FetchCtx.opcode >= OP_LDIR && FetchCtx.opcode <= OP_RESR)) DecodeCtx.regwrite = true;
	if (FetchCtx.opcode >= OP_LD && FetchCtx.opcode <= OP_LDHZ) DecodeCtx.memwrite = true;

	if (FetchCtx.opcode >= OP_STR && FetchCtx.opcode <= OP_STRH) MemoryCtx.write = true;
	else MemoryCtx.write = false;

	if (FetchCtx.opcode == OP_ADDS || FetchCtx.opcode == OP_SUBS || FetchCtx.opcode == OP_CMP) DecodeCtx.setCC = true;

	if (FetchCtx.opcode == OP_B) {
		ExecuteCtx.cond = checkCondition();
		dLog(D_NONE, DSEV_INFO, "Condition marked as %d", ExecuteCtx.cond);
	}

	decideALUOp();
	dLog(D_NONE, DSEV_INFO, "decode::ALU OP: %s", ALUOP_STR[DecodeCtx.aluop]);

	regfile(false);
	dLog(D_NONE, DSEV_INFO, "decode::Reg A val: 0x%x; Reg B val: 0x%x", DecodeCtx.vala, DecodeCtx.valb);

	// Val ex for M types contain
}

static void execute() {
	if (FetchCtx.opcode == OP_SYSCALL) exception(core.GPR[0]);

	if (core.status == STAT_EXCP) return;

	ExecuteCtx.aluVala = DecodeCtx.vala;
	
	if (DecodeCtx.iType == I_TYPE || DecodeCtx.iType == M_TYPE) ExecuteCtx.aluValb = DecodeCtx.imm;
	else ExecuteCtx.aluValb = DecodeCtx.valb;

	// M types can either use imm as aluvalb or valb (index reg) as aluvalb
	if (DecodeCtx.iType == M_TYPE) {
		if (DecodeCtx.imm == 0) {
			// It is difficult to detect the usage based off on the bits (for now, maybe use some bits as indication???)
			// On assembling, if no offset is used, it is stored as 0, if no index is used, it is stored as x30
			// meaning it could go like xd, [x, #0], [x30]
			// Zero will be added to anyway
			// Thus assume if no imm (#0), it uses index (rr/valb)
			// If by coincidence, imm is #0 (no much point but for clarity can be done), the presence of x30 takes care of it
			ExecuteCtx.aluValb = DecodeCtx.valb;
		}
		// Else is aluValb = imm, which can be done in earlier code
	}

	dDebug(DB_TRACE, "Current IR: 0x%x", core.IR);
	nextIR();
	dLog(D_NONE, DSEV_INFO, "execute::Next IR: 0x%x", core.IR);
	dLog(D_NONE, DSEV_INFO, "execute::ALU Val a: 0x%x; ALU Val b: 0x%x;", ExecuteCtx.aluVala, ExecuteCtx.aluValb);

	alu();

	// Either MVCSTR has ALU_ADD that adds the contents of rs and 0 so it maintins rs in alures so it can be written to CSTR
	// Or it is overwritten with rs (vala)
	if (FetchCtx.opcode == OP_MVCSTR) ExecuteCtx.alures = DecodeCtx.vala;

	dLog(D_NONE, DSEV_INFO, "execute::Val res: 0x%x", ExecuteCtx.alures);

	if (FetchCtx.opcode >= OP_STR && FetchCtx.opcode <= OP_STRH) MemoryCtx.valmem = DecodeCtx.valex;

	if (FetchCtx.opcode == OP_HLT) {
		// HALT can either be a normal halt or an IO halt, depending on bit 13 of CSTR
		if (((core.CSTR >> 13) & 0b1) == 0b1) core.status = STAT_IO;
		else core.status = STAT_HLT;
	}

	// fpu();
	// vcu();
}

static void memory() {
	if (core.status == STAT_EXCP) return;

	// if (FetchCtx.opcode >= OP_STR && FetchCtx.opcode <= OP_STRH) {
	// 	Mem
	// }

	memerr_t err = MEMERR_NONE;

	if (MemoryCtx.write) {
		dLog(D_NONE, DSEV_INFO, "memory::Writing 0x%x to memory address 0x%x", MemoryCtx.valmem, ExecuteCtx.alures);
		dmem(ExecuteCtx.alures, NULL, &(MemoryCtx.valmem), &err);
	}	else {
		dmem(ExecuteCtx.alures, &(ExecuteCtx.aluValb), NULL, &err);
		dLog(D_NONE, DSEV_INFO, "memory::Read 0x%x from memory address 0x%x", ExecuteCtx.aluValb, ExecuteCtx.alures);
	}

	if (err == MEMERR_INTERNAL) {
		dLog(D_NONE, DSEV_WARN, "Something went wrong with execute context!");
		fault();
	}

	if (err != MEMERR_NONE) {
		if (GET_PRIV(core.CSTR) == PRIVILEGE_KERNEL) {
			switch (err) {
				case MEMERR_KERN_OVERFLOW:
					dLog(D_NONE, DSEV_WARN, "Detected kernel overflow!");
					break;
				case MEMERR_KERN_OVERREAD:
					dLog(D_NONE, DSEV_WARN, "Detected kernel overread!");
					break;
				case MEMERR_KERN_STACK_OVERFLOW:
					dLog(D_NONE, DSEV_WARN, "Detected kernel stack overflow!");
					break;
				case MEMERR_KERN_HEAP_OVERFLOW:
					dLog(D_NONE, DSEV_WARN, "Detected kernel heap overflow!");
					break;
				case MEMERR_KERN_TEXT_WRITE:
					dLog(D_NONE, DSEV_WARN, "Detected kernel writing to text!");
					break;
				case MEMERR_KERN_SECT_WRITE:
					dLog(D_NONE, DSEV_WARN, "Detected kernel writing to invalid memory!");
					break;
				case MEMERR_KERN_SECT_READ:
					dLog(D_NONE, DSEV_WARN, "Detected kernel reading from invalid memory!");
					break;
				default:
					break;
			}
			fault();
		} else {
			switch (err) {
				case MEMERR_USER_OVERFLOW:
					dLog(D_NONE, DSEV_WARN, "Detected user overflow!");
					break;
				case MEMERR_USER_OVERREAD:
					dLog(D_NONE, DSEV_WARN, "Detected user overread!");
					break;
				case MEMERR_USER_STACK_OVERFLOW:
					dLog(D_NONE, DSEV_WARN, "Detected user stack overflow!");
					break;
				case MEMERR_USER_HEAP_OVERFLOW:
					dLog(D_NONE, DSEV_WARN, "Detected user heap overflow!");
					break;
				case MEMERR_USER_TEXT_WRITE:
					dLog(D_NONE, DSEV_WARN, "Detected user writing to text!");
					break;
				case MEMERR_USER_CONST_WRITE:
					dLog(D_NONE, DSEV_WARN, "Detected user writing to const!");
					break;
				case MEMERR_USER_SECT_WRITE:
					dLog(D_NONE, DSEV_WARN, "Detected user writing to invalid memory!");
					break;
				case MEMERR_USER_SECT_READ:
					dLog(D_NONE, DSEV_WARN, "Detected user reading from invalid memory!");
					break;
				default:
					break;
			}
			exception(EXCPN_ABORT_ACCESS);
		}
	}


	if (DecodeCtx.memwrite) MemoryCtx.valout = ExecuteCtx.aluValb;
	else MemoryCtx.valout = ExecuteCtx.alures;

	regfile(DecodeCtx.regwrite);
}

void initCore() {
	for (int i = 0; i < 31; i++) {
		core.GPR[i] = 0x00000000;
	}

	core.IR = 0x00000000;
	core.SP = 0x00000000;

	for (int i = 0; i < 16; i++) {
		core.FPR[i] = 0.0;
	}

	for (int i = 0; i < 6; i++) {
		core.VR[i]._v32[0] = 0x00000000;
		core.VR[i]._v32[1] = 0x00000000;
		core.VR[i]._v32[2] = 0x00000000;
		core.VR[i]._v32[3] = 0x00000000;
	}

	core.CSTR = 0x0000;
	core.ESR = 0x0000;

	memset(&core.uarch, 0x0, sizeof(InstrCtx));
}

void viewCoreState() {
	dLog(D_NONE, DSEV_INFO, "Core state:\n\tIR: 0x%x\n\tSP: 0x%x\n\tCSTR: 0x%x\n\tRegisters:\n\
\tX0/XR/A0: 0x%x\n\tX1/A1: 0x%x\n\tX2/A2: 0x%x\n\tX3/A3: 0x%x\n\tX4/A4: 0x%x\n\tX5/A5: 0x%x\n\
\tX6/A6: 0x%x\n\tX7/A7: 0x%x\n\tX8/A8: 0x%x\n\tX9/A9: 0x%x\n\tX10: 0x%x\n\tX11: 0x%x\n\
\tX12/C0: 0x%x\n\tX13/C1: 0x%x\n\tX14/C2: 0x%x\n\tX15/C3: 0x%x\n\tX16/C4: 0x%x\n\tX17/S0: 0x%x\n\
\tX18/S1: 0x%x\n\tX19/S2: 0x%x\n\tX20/S3: 0x%x\n\tX21/S4: 0x%x\n\tX22/S5: 0x%x\n\tX23/S6: 0x%x\n\
\tX24/S7: 0x%x\n\tX25/S8: 0x%x\n\tX26/S9: 0x%x\n\tX27/S10: 0x%x\n\tX28/LR: 0x%x\n\tX9/SB: 0x%x\n\n",
		core.IR, core.SP, core.CSTR, 
		core.GPR[0], core.GPR[1], core.GPR[2], core.GPR[3], core.GPR[4], core.GPR[5],
		core.GPR[6], core.GPR[7], core.GPR[8], core.GPR[9], core.GPR[10], core.GPR[11],
		core.GPR[12], core.GPR[13], core.GPR[14], core.GPR[15], core.GPR[16], core.GPR[17],
		core.GPR[18], core.GPR[19], core.GPR[20], core.GPR[21], core.GPR[22], core.GPR[23],
		core.GPR[24], core.GPR[25], core.GPR[26], core.GPR[27], core.GPR[28], core.GPR[29]);

	// fflush(stdout);
}

void* runCore(void* _) {
	dLog(D_NONE, DSEV_INFO, "Executing core thread...");
	core.status = STAT_RUNNING;

	int runningCycles = 0;
	int idleCycles = 0;
	while (true) {
		pthread_mutex_lock(&idleLock);
		if (!IDLE) {
			dLog(D_NONE, DSEV_INFO, "\nCycle %d", runningCycles);
			fetch();
			decode();
			execute();
			memory();

			if (FetchCtx.opcode == OP_ERET) {
				core.CSTR = userPS->cstr;
				// Saved CSTR contains the mode bit to kernel mode, reset it
				core.CSTR = CLR_PRIV(core.CSTR);
			}

			runningCycles++;

			// viewCoreState();

			// In case of an infinite loop or something, limit how much it can cycle for
			if (runningCycles > 500) core.status = STAT_HLT;
			dDebug(DB_BASIC, "Heap pointer (VA) 0x%x", *((uint32_t*)(emMem + KERN_DATA)));
			if (core.status == STAT_HLT) {
				dLog(D_NONE, DSEV_INFO, "Going idle");
				IDLE = true;

				// Check if normal halt or abort halt for user programs
				if (userPS) {
					excp_t _exception = userPS->excpType;

					if (_exception != EXCP_SYSCALL) {
						dLog(D_NONE, DSEV_INFO, "Detected abort fault...");

						// Send SIG_FAULT to emulator
						signal_t* sig = GET_SIGNAL(sigMem->signals, UNIVERSAL_SIG);
						sigMem->metadata.signalType = UNIVERSAL_SIG;
						setFaultSignal(sig);
						kill(sigMem->metadata.emulatorPID, SIGUSR1);

						// Block until emulator acks
						uint8_t ackd = 0x0;
						while (ackd != 0x1) ackd = SIG_GET(sig->ackMask, emSIG_FAULT_IDX);

						// Send SIG_ERROR to shell
						sig = GET_SIGNAL(sigMem->signals, SHELL_CPU_SIG);
						setErrorSignal(sig);
					} else { // for syscall, if any, continue
						// shell is the only one waiting on program exit
						// update SIG_EXIT
						signal_t* sig = GET_SIGNAL(sigMem->signals, SHELL_CPU_SIG);
						sig->interrupts = SIG_SET(sig->interrupts, emSIG_EXIT_IDX);
					}
				} else pthread_cond_signal(&idleCond);

				// Reset cycles???
				runningCycles = 0;
			} else if (core.status == STAT_IO) {
				dLog(D_NONE, DSEV_INFO, "Going idle from IO request");
				IDLE = true;

				signal_t* sig = GET_SIGNAL(sigMem->signals, EMU_CPU_SIG);
				sigMem->metadata.signalType = EMU_CPU_SIG;

				syscall_md metadata = {
					.ioReq.kerneldataPtr = core.GPR[10]
				};

				dDebug(DB_BASIC, "Pointer to kernel data structure: 0x%x (%p)", core.GPR[10], emMem + core.GPR[10]);
				uint32_t* strc = (uint32_t*)(emMem + core.GPR[10]);
				dDebug(DB_BASIC, "struct->fd (0x%x [%p]): %d; struct->count (0x%x [%p]): %d; sturct->buffer (0x%x [%p]): 0x%x", 
					(core.GPR[10] + 0), (strc+0), *(strc + 0), (core.GPR[10] + 4), (strc+1), *(strc + 1), (core.GPR[10] + 8), (strc+1), *(strc + 1));

				setSysSignal(sig, &metadata);
				kill(sigMem->metadata.emulatorPID, SIGUSR1);
			}
		} else {
			pthread_mutex_unlock(&idleLock);
			idleCycles++;
			continue;
		}
		pthread_mutex_unlock(&idleLock);
	}

	return NULL;
}