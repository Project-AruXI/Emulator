#include <stdlib.h>
#include <string.h>

#include "emSignal.h"
#include "sigHeap.h"


void setupSignals(SigMem* signalMemory) {
	// This can only run once!!!
	// At first, there's no distiniction, order can change at any time based on this implementation

	signalMemory->metadata.emulatorPID = -1;
	signalMemory->metadata.shellPID = -1;
	signalMemory->metadata.cpuPID = -1;
	signalMemory->metadata.signalType = 0;

	signal_t* sigs = signalMemory->signals;

	signal_t* universalSignals = GET_SIGNAL(sigs, UNIVERSAL_SIG);
	signal_t* emuShellSignals = GET_SIGNAL(sigs, EMU_SHELL_SIG);
	signal_t* emuCPUSignals = GET_SIGNAL(sigs, EMU_CPU_SIG);
	signal_t* shellCPUSignals = GET_SIGNAL(sigs, SHELL_CPU_SIG);

	universalSignals->interrupts = 0x0;
	universalSignals->intEnable = emSIG_SHUTDOWN | emSIG_FAULT | emSIG_READY;
	universalSignals->ackMask = 0x0;
	universalSignals->payloadValid = 0x0;
	memset(&universalSignals->metadata, 0x0, sizeof(pd_metadata));
	
	emuShellSignals->interrupts = 0x0;
	emuShellSignals->intEnable = emSIG_LOAD | emSIG_IO;
	emuShellSignals->ackMask = 0x0;
	emuShellSignals->payloadValid = 0x0;
	memset(&emuShellSignals->metadata, 0x0, sizeof(pd_metadata));

	emuCPUSignals->interrupts = 0x0;
	emuCPUSignals->intEnable = emSIG_CPU_SAVE | emSIG_SYS;
	emuCPUSignals->ackMask = 0x0;
	emuCPUSignals->payloadValid = 0x0;
	memset(&emuCPUSignals->metadata, 0x0, sizeof(pd_metadata));

	shellCPUSignals->interrupts = 0x0;
	shellCPUSignals->intEnable = emSIG_ERROR | emSIG_EXIT | emSIG_KILL | emSIG_EXEC;
	shellCPUSignals->ackMask = 0x0;
	shellCPUSignals->payloadValid = 0x0;
	memset(&shellCPUSignals->metadata, 0x0, sizeof(pd_metadata));
}


int setShutdownSignal(signal_t* signal) {
	uint8_t enable = SIG_GET(signal->intEnable, emSIG_SHUTDOWN_IDX);
	if (enable != 1) return -1;

	signal->interrupts = SIG_SET(signal->interrupts, emSIG_SHUTDOWN_IDX);

	return SIG_GET(signal->interrupts, emSIG_SHUTDOWN_IDX);
}

int ackShutdownSignal(signal_t* signal) {
	uint8_t enable = SIG_GET(signal->intEnable, emSIG_SHUTDOWN_IDX);
	if (enable != 1) return -1;

	signal->ackMask = SIG_SET(signal->ackMask, emSIG_SHUTDOWN_IDX);

	return SIG_GET(signal->ackMask, emSIG_SHUTDOWN_IDX);
}

int setFaultSignal(signal_t* signal) {
	uint8_t enable = SIG_GET(signal->intEnable, emSIG_FAULT_IDX);
	if (enable != 1) return -1;

	signal->interrupts = SIG_SET(signal->interrupts, emSIG_FAULT_IDX);

	return SIG_GET(signal->interrupts, emSIG_FAULT_IDX);
}

int ackFaultSignal(signal_t* signal) {
	uint8_t enable = SIG_GET(signal->intEnable, emSIG_FAULT_IDX);
	if (enable != 1) return -1;

	signal->ackMask = SIG_SET(signal->ackMask, emSIG_FAULT_IDX);

	return SIG_GET(signal->ackMask, emSIG_FAULT_IDX);
}

int setReadySignal(signal_t* signal) {
	uint8_t enable = SIG_GET(signal->intEnable, emSIG_READY_IDX);
	if (enable != 1) return -1;

	signal->interrupts = SIG_SET(signal->interrupts, emSIG_READY_IDX);

	return SIG_GET(signal->interrupts, emSIG_READY_IDX);
}

int ackReadySignal(signal_t* signal) {
	uint8_t enable = SIG_GET(signal->intEnable, emSIG_READY_IDX);
	if (enable != 1) return -1;

	signal->ackMask = SIG_SET(signal->ackMask, emSIG_READY_IDX);

	return SIG_GET(signal->ackMask, emSIG_READY_IDX);
}


int setSysSignal(signal_t* signal, syscall_md* metadata) {
	uint8_t enable = SIG_GET(signal->intEnable, emSIG_SYS_IDX);
	if (enable != 1) return -1;

	signal->interrupts = SIG_SET(signal->interrupts, emSIG_SYS_IDX);
	signal->payloadValid = SIG_SET(signal->payloadValid, emSIG_SYS_IDX);

	signal->metadata.syscall.ioReq.kerneldataPtr = metadata->ioReq.kerneldataPtr;

	return SIG_GET(signal->interrupts, emSIG_SYS_IDX);;
}

int ackSysSignal(signal_t* signal) {
	uint8_t enable = SIG_GET(signal->intEnable, emSIG_SYS_IDX);
	if (enable != 1) return -1;

	signal->ackMask = SIG_SET(signal->ackMask, emSIG_SYS_IDX);
	signal->payloadValid = SIG_CLR(signal->payloadValid, emSIG_SYS_IDX);

	return SIG_GET(signal->ackMask, emSIG_SYS_IDX);
}

int setIOSignal(signal_t* signal, syscall_md* metadata) {
	uint8_t enable = SIG_GET(signal->intEnable, emSIG_IO_IDX);
	if (enable != 1) return -1;

	signal->interrupts = SIG_SET(signal->interrupts, emSIG_IO_IDX);
	signal->payloadValid = SIG_SET(signal->payloadValid, emSIG_IO_IDX);

	if (signal->metadata.syscall.ioData.bufferOffset != 0) {
		char* buffer = offsetToPtr(signal->metadata.syscall.ioData.bufferOffset);
		sfree(buffer);
	}

	signal->metadata.syscall.ioData.bufferOffset = metadata->ioData.bufferOffset;
	signal->metadata.syscall.ioData.count = metadata->ioData.count;
	signal->metadata.syscall.ioData.stream = metadata->ioData.stream;

	return SIG_GET(signal->ackMask, emSIG_IO_IDX);
}

int ackIOSignal(signal_t* signal) {
	return 0;
}

int setLoadSignal(signal_t* signal, loadprog_md* metadata) {
	uint8_t enable = SIG_GET(signal->intEnable, emSIG_LOAD_IDX);
	if (enable != 1) return -1;

	signal->interrupts = SIG_SET(signal->interrupts, emSIG_LOAD_IDX);
	signal->payloadValid = SIG_SET(signal->payloadValid, emSIG_LOAD_IDX);

	// Free since pointers will be replaced
	// `program` is part of `argv`
	if (signal->metadata.loadprog.argvOffset != 0) {
		// Offset being 0 means that it is at the start of the heap, which is not possible as the start is the metadata
		char** argv = offsetToPtr(signal->metadata.loadprog.argvOffset);

		for (int i = 0; i < signal->metadata.loadprog.argc; i++) {
			sfree(argv[i]);
		}

		sfree(argv);
	}

	signal->metadata.loadprog.programOffset = metadata->programOffset;
	signal->metadata.loadprog.argc = metadata->argc;
	signal->metadata.loadprog.argvOffset = metadata->argvOffset;

	return SIG_GET(signal->interrupts, emSIG_LOAD_IDX);
}

int ackLoadSignal(signal_t* signal) {
	uint8_t enable = SIG_GET(signal->intEnable, emSIG_LOAD_IDX);
	if (enable != 1) return -1;

	signal->ackMask = SIG_SET(signal->ackMask, emSIG_LOAD_IDX);

	return SIG_GET(signal->ackMask, emSIG_LOAD_IDX);
}


int setErrorSignal(signal_t* signal) { 
	uint8_t enable = SIG_GET(signal->intEnable, emSIG_ERROR_IDX);
	if (enable != 1) return -1;

	signal->interrupts = SIG_SET(signal->interrupts, emSIG_ERROR_IDX);

	return SIG_GET(signal->interrupts, emSIG_ERROR_IDX);
}

int ackErrorSignal(signal_t* signal) { return 0; }

int setExitSignal(signal_t* signal) { return 0; }

int ackExitSignal(signal_t* signal) { return 0; }

int setKillSignal(signal_t* signal) { return 0; }

int ackKillSignal(signal_t* signal) { return 0; }

int setExecSignal(signal_t* signal, execprog_md* metadata) {
	// Make sure the signal entry allows for exec
	uint8_t enable = SIG_GET(signal->intEnable, emSIG_EXEC_IDX);
	if (enable != 1) return -1;

	signal->interrupts = SIG_SET(signal->interrupts, emSIG_EXEC_IDX);
	signal->payloadValid = SIG_SET(signal->payloadValid, emSIG_EXEC_IDX);

	// Fill out metadata
	signal->metadata.execprog.entry = metadata->entry;

	return SIG_GET(signal->interrupts, emSIG_EXEC_IDX);
}

int ackExecSignal(signal_t* signal) {
	uint8_t enable = SIG_GET(signal->intEnable, emSIG_EXEC_IDX);
	if (enable != 1) return -1;

	signal->ackMask = SIG_SET(signal->ackMask, emSIG_EXEC_IDX);

	return SIG_GET(signal->ackMask, emSIG_EXEC_IDX);
}