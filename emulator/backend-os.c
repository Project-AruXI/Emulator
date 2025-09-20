#include <unistd.h>

#include "emSignal.h"
#include "signalHandler.h"
#include "diagnostics.h"
#include "sigHeap.h"
#include "backend-os.h"


extern void* emulatedMemory;
extern SigMem* signalsMemory;


static void fault() {
	signalsMemory->metadata.signalType = UNIVERSAL_SIG;
	signal_t* sig = GET_SIGNAL(signalsMemory->signals, UNIVERSAL_SIG);
	setFaultSignal(sig);

	kill(signalsMemory->metadata.cpuPID, SIGUSR1);
	kill(signalsMemory->metadata.shellPID, SIGUSR1);
}

void ruWrite(const char* buffer, uint32_t count) {
	dDebug(DB_BASIC, "Writing buffer of count %d", count);
	char* _buffer = smalloc(count);
	if (!_buffer) {
		fault();
		dFatal(D_ERR_MEM, "Could not allocate memory for shared signals.");
	}

	bool valid = true;

	signalsMemory->metadata.signalType = EMU_SHELL_SIG;
	signal_t* emuShellSig = GET_SIGNAL(signalsMemory->signals, EMU_SHELL_SIG);
	syscall_md metadata = {
		.ioData.bufferOffset = ptrToOffset(_buffer, &valid),
		.ioData.count = count,
		.ioData.stream = ARU_STDOUT
	};

	if (!valid) {
		fault();
		dFatal(D_ERR_INTERNAL, "Invalid pointer to signal heap: %p", _buffer);
	}

	setIOSignal(emuShellSig, &metadata);
	kill(signalsMemory->metadata.shellPID, SIGUSR1);

	// Block until it has been written
	uint8_t ackd = 0x0;
	while (ackd != 0x1) ackd = SIG_GET(emuShellSig->ackMask, emSIG_IO_IDX);
	emuShellSig->ackMask = SIG_CLR(emuShellSig->ackMask, emSIG_IO_IDX);
}

void ruRead(char *buffer, uint32_t count) {
}