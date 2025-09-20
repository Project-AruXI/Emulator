#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#include "emSignal.h"
#include "signalHandler.h"
#include "aoef.h"
#include "diagnostics.h"
#include "shmem.h"
#include "loader.h"
#include "sigHeap.h"
#include "backend-os.h"


#define KERN_START 0xA0080000 
#define KERN_DATA 0xA0080000
#define KERN_TEXT 0xB8080000
#define KERN_HEAP 0xD0080000
#define KERN_STACK 0xF0080000
#define EVT_START 0x00040000


SigMem* signalsMemory;
void* emulatedMemory;


/**
 * 
 */
static void handleLoadSignal(signal_t* emuShellSig) {
	char* filename = (char*) offsetToPtr(emuShellSig->metadata.loadprog.programOffset);

	uint32_t userEntry = loadBinary(filename, emulatedMemory);

	// Place entry point at the top of kernel stack
	// For some reason, the pointer needs to be type uint32 so it writes all of the bits of the entry
	*(uint32_t*)((uint8_t*)emulatedMemory + 0xFFFFFFFB) = userEntry;

	// Place arv/argc in user stack
	// TODO later

	// Ack it so shell can know
	emuShellSig->ackMask = SIG_SET(emuShellSig->ackMask, emSIG_LOAD_IDX);
	// write(STDOUT_FILENO, "Acked\n", 6);
}

/**
 * 
 * @param emuCPUSig 
 */
static void handleFaultSignal(signal_t* emuCPUSig) {
	// Only time emulator gets fault signal is on user abort exception
	// Do a "coredump"
	uint32_t ___psPtr = KERN_DATA + 0x4; // The virtual address where the pointer to PS is stored
	uint8_t* __psPtr = emulatedMemory + ___psPtr; // The real address where the pointer to PS is stored
	uint32_t _psPtr = *((uint32_t*)__psPtr); // The virtual address of PS
	uint8_t* psPtr = (((uint8_t*)emulatedMemory) + _psPtr);
	// Unlike in cpu/core, emulator does not know the structure of PS
	// No need to do an unncessary "import"
	// Just go with offsets :)

	uint8_t userPID = *((uint8_t*)(psPtr + 0));
	uint8_t userThreadc = *((uint8_t*)(psPtr + 1));
	// userThreadStates
	uint32_t userSP = *((uint32_t*)(psPtr + 6));
	uint32_t userIR = *((uint32_t*)(psPtr + 10));
	uint16_t userCSTR = *((uint16_t*)(psPtr + 14));
	uint16_t userESR = *((uint16_t*)(psPtr + 16));
	uint32_t* userGPR = (uint32_t*)(psPtr + 18);
	float* userFPR = (float*)(psPtr + 118);
	// userVr
	uint8_t userExcpType = *(psPtr + 566);

	FILE* dump = fopen("iaru0.admp", "w");
	if (!dump) {
		dLog(D_ERR_IO, DSEV_WARN, "Could not open dump file");
		signalsMemory->metadata.signalType = UNIVERSAL_SIG;
		signal_t* sig = GET_SIGNAL(signalsMemory->signals, UNIVERSAL_SIG);
		setFaultSignal(sig);

		kill(signalsMemory->metadata.cpuPID, SIGUSR1);
		kill(signalsMemory->metadata.shellPID, SIGUSR1);
		exit(-1);
	}

	fprintf(dump, "PID: %d\nThreadc: %d\nSP: 0x%x\nIR: 0x%x\nCSTR: 0x%x\nESR: 0x%x\n",
		userPID, userThreadc, userSP, userIR, userCSTR, userESR);

	fprintf(dump,
		"X0/A0/XR: 0x%x\nX1/A1: 0x%x\nX2/A2: 0x%x\nX3/A3: 0x%x\nX4/A4: 0x%x\nX5/A5: 0x%x\nX6/A6: 0x%x\nX7/A7: 0x%x\nX8/A8: 0x%x\nX9/A9: 0x%x\n",
		userGPR[0], userGPR[1], userGPR[2], userGPR[3], userGPR[4], userGPR[5], userGPR[6], userGPR[7], userGPR[8], userGPR[9]
	);

	fprintf(dump,
		"X10: 0x%x\nX11: 0x%x\nX12/C0: 0x--\nX13/C1: 0x--\nX14/C2: 0x--\nX15/C3: 0x--\nX16/C4: 0x--\nX17/S0: 0x%x\nX18/S1: 0x%x\nX19/S2: 0x%x\n",
		userGPR[10], userGPR[11], /*userGPR[2], userGPR[3], userGPR[4], userGPR[5], userGPR[6], */userGPR[12], userGPR[13], userGPR[14]
	);

	fprintf(dump,
		"X20/S3: 0x%x\nX21/S4: 0x%x\nX22/S5: 0x%x\nX23/S6: 0x%x\nX24/S7: 0x%x\nX25/S8: 0x%x\nX26/S9: 0x%x\nX27/S10: 0x%x\nX28/LR: 0x%x\nX29/XB: 0x%x\n",
		userGPR[15], userGPR[16], userGPR[17], userGPR[18], userGPR[19], userGPR[20], userGPR[21], userGPR[22], userGPR[23], userGPR[24]
	);

	fprintf(dump, "excpType: %s", (userExcpType == 0b00) ? "SYSCALL" : ((userExcpType == 0b01) ? "DATA ABORT" : "FETCH ABORT"));

	fclose(dump);

	emuCPUSig->ackMask = SIG_SET(emuCPUSig->ackMask, emSIG_FAULT_IDX);
}

static void handleSysSignal(signal_t* emuCPUSig) {
	uint32_t* data = (uint32_t*)(emulatedMemory + emuCPUSig->metadata.syscall.ioReq.kerneldataPtr);
	
	dDebug(DB_DETAIL, "data struct from em mem: %p (VA 0x%x) = {buffer=0x%x, count=0x%x}", 
			data, emuCPUSig->metadata.syscall.ioReq.kerneldataPtr, *(data + 2), *(data + 1));

	char* buffer = (char*) (emulatedMemory + *(data + 2));

	switch (*(data + 0))	{
		case ARU_STDOUT:
			ruWrite((const char*) buffer, *(data + 1));
			break;
		case ARU_STDIN:
			ruRead(buffer, *(data + 1));
			break;
		default:
			break;
	}


	// Let CPU know
	signalsMemory->metadata.signalType = EMU_CPU_SIG;
	emuCPUSig->ackMask = SIG_SET(emuCPUSig->ackMask, emSIG_SYS_IDX);
	kill(signalsMemory->metadata.cpuPID, SIGUSR1);
}

/**
 * Handle SIGUSR1. SIGUSR1 indicates as a poke to tell the process to check the signal memory.
 * @param signum 
 */
void handleSIGUSR1(int signum) {
	// Check metadata
	write(STDOUT_FILENO, "Got SIGUSR1\n", 12);
	uint8_t sigType = signalsMemory->metadata.signalType;
	signal_t* sig = GET_SIGNAL(signalsMemory->signals, sigType);

	uint32_t ints = sig->interrupts;
	int i = 0;

	while (i <= 31) {
		uint8_t bit = (ints >> i) & 0b1;

		if (bit == 0b1) {
			switch (i) {
				case emSIG_LOAD_IDX:
					write(STDOUT_FILENO, "Emulator detected SIG_LOAD\n", 27);
					handleLoadSignal(sig);
					break;
				case emSIG_FAULT_IDX:
					write(STDOUT_FILENO, "Emulator detected SIG_FAULT\n", 28);
					handleFaultSignal(sig);
					break;
				case emSIG_SYS_IDX:
					write(STDOUT_FILENO, "Emulator detected SIG_SYS\n", 26);
					handleSysSignal(sig);
					break;
				default:
					break;
			}
		}

		i++;
	}
}

void handleSIGSEGV(int signum) {
	write(STDOUT_FILENO, "Emulator got SIGSEGV'd\n", 23);
	fflush(stderr);
	fflush(stdout);
	flushDebug();

	signalsMemory->metadata.signalType = UNIVERSAL_SIG;
	signal_t* sig = GET_SIGNAL(signalsMemory->signals, UNIVERSAL_SIG);
	setFaultSignal(sig);

	kill(signalsMemory->metadata.cpuPID, SIGUSR1);
	kill(signalsMemory->metadata.shellPID, SIGUSR1);

	exit(-1);
}

/**
 * Loads the kernel image binary into an mmap'd space for easy of acess, checking that the binary is in the proper format.
 * @param kernimg 
 * @return The pointer to the mmapd' binary
 */
static void* loadKernel(char* kernimg) {
	int fd = open(kernimg, O_RDONLY);
	if (fd < 0) dFatal(D_ERR_IO, "Could not open kernel image %s!", kernimg);

	struct stat statBuffer;
	int rc = fstat(fd, &statBuffer);
	if (rc != 0) dFatal(D_ERR_IO, "Could not stat file descriptor!");

	void* ptr = mmap(0, statBuffer.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (ptr == MAP_FAILED) dFatal(D_ERR_INTERNAL, "Could not map file!");
	close(fd);

	AOEFFheader* header = (AOEFFheader*) ptr;

	// Check it is an AOEFF and it is type kernel
	if (header->hID[AHID_0] != AH_ID0 && header->hID[AHID_1] != AH_ID1 && 
			header->hID[AHID_2] != AH_ID2 && header->hID[AH_ID3] != AH_ID3) dFatal(D_ERR_INVALID_FORMAT, "File is not an AOEFF!");

	if (header->hType != AHT_KERN) dFatal(D_ERR_INVALID_FORMAT, "File is not a kernel image!");

	return ptr;
}

static void* createMemory() {
	int fd = shm_open(SHMEM_MEM, O_CREAT | O_RDWR, 0666);
	if (fd == -1) dFatal(D_ERR_SHAREDMEM, "Could not open shared memory for emulated memory!");

	int r = ftruncate(fd, MEMORY_SPACE_SIZE);
	if (r == -1) dFatal(D_ERR_INTERNAL, "Could not ftruncate!");

	void* emMem = mmap(NULL, MEMORY_SPACE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (emMem == MAP_FAILED) dFatal(D_ERR_MEM, "Could not mmap emulated memory!");

	close(fd);
	return emMem;
}

static SigMem* createSignalMemory() {
	int fd = shm_open(SHMEM_SIG, O_CREAT | O_RDWR, 0666);
	if (fd == -1) dFatal(D_ERR_SHAREDMEM, "Could not open shared memory for signal!");

	int r = ftruncate(fd, SIG_MEM_SIZE);
	if (r == -1) dFatal(D_ERR_INTERNAL, "Could not ftruncate!");

	void* _sigMem = mmap(NULL, SIG_MEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (_sigMem == MAP_FAILED) dFatal(D_ERR_MEM, "Could not mmap signal memory!");

	close(fd);

	dDebug(DB_BASIC, "Signal Memory created at %p", _sigMem);

	SigMem* sigMem = (SigMem*) _sigMem;


	// Create the shared heap
	fd = shm_open(SHMEM_HEAP, O_CREAT | O_RDWR, 0666);
	if (fd == -1) dFatal(D_ERR_SHAREDMEM, "Could not open shared memory for signal heap!");

	r = ftruncate(fd, PAGESIZE);
	if (r == -1) dFatal(D_ERR_INTERNAL, "Could not ftruncate!");

	void* _sigHeap = mmap(NULL, PAGESIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (_sigHeap == MAP_FAILED) dFatal(D_ERR_MEM, "Could not mmap signal heap!");

	close(fd);

	dDebug(DB_BASIC, "Signal Heap created at %p!", _sigHeap);

	sigMem->metadata.heap[EMU_HEAP] = _sigHeap;

	sinit(_sigHeap, true);

	return sigMem;
}

static void setupKernel(uint8_t* memory, uint8_t* kernimg, signal_t* sigs) {
	AOEFFheader* header = (AOEFFheader*) kernimg;
	uint32_t kernEntry = header->hEntry;

	AOEFFSectHeader* sectHdrs = (AOEFFSectHeader*)(kernimg + header->hSectOff);
	uint32_t sectHdrsSize = header->hSectSize;

	// Set the data and text information
	for (uint32_t i = 0; i < sectHdrsSize; i++) {
		AOEFFSectHeader* sectHdr = &(sectHdrs[i]);

		if (strncmp(".data", sectHdr->shSectName, 8) == 0) {
			uint8_t* dataStart = memory + KERN_DATA; // beginning of emulated memory kernel data
			uint8_t* kernimgData = kernimg + sectHdr->shSectOff; // beginning of binary image kernel data section

			dDebug(DB_DETAIL, "Start of data section in kernel image: %p::Start of kernel data section in emulated memory:%p", kernimgData, dataStart);
			dDebug(DB_DETAIL, "First item in data: 0x%x from 0x%x", *(dataStart), *(kernimgData));

			memcpy(dataStart, kernimgData, sectHdr->shSectSize);
		} else if (strncmp(".text", sectHdr->shSectName, 8) == 0) {
			uint8_t* textStart = memory + KERN_TEXT;
			uint8_t* kernimgText = kernimg + sectHdr->shSectOff;

			dDebug(DB_DETAIL, "Start of text section in kernel image: %p::Start of kernel text section in emulated memory:%p", kernimgText, textStart);
			dDebug(DB_DETAIL, "First item in text: 0x%x from 0x%x", *(textStart), *(kernimgText));

			memcpy(textStart, kernimgText, sectHdr->shSectSize);
		} else if (strncmp(".evt", sectHdr->shSectName, 8) == 0) {
			uint8_t* evtStart = memory + EVT_START;
			uint8_t* kernimgEvt = kernimg + sectHdr->shSectOff;

			dDebug(DB_DETAIL, "Start of EVT section in kernel image: %p::Start of EVT section in emulated memory:%p", kernimgEvt, evtStart);
			dDebug(DB_DETAIL, "First item in EVT: 0x%x from 0x%x", *(evtStart), *(kernimgEvt));

			memcpy(evtStart, kernimgEvt, sectHdr->shSectSize);
		}
	}

	// Even though this is the emulator, exec signal is only available for shell-cpu
	signal_t* shellCPUSignal = GET_SIGNAL(sigs, SHELL_CPU_SIG);
	execprog_md metadata = {
		.entry = kernEntry
	};
	int ret = setExecSignal(shellCPUSignal, &metadata);
	if (ret == -1) dFatal(D_ERR_SIGNAL, "No access for exec signal!");
	dLog(D_NONE, DSEV_INFO, "Kernel has been set up. Exec signal has now been set.");
}

static void redirectOut(const char* filename) {
	int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd == -1) dFatal(D_ERR_IO, "Could not open logfile!");

	dup2(fd, STDOUT_FILENO);
	dup2(fd, STDERR_FILENO);
	close(fd);
}

static pid_t openShell(char* shellExe, bool log) {
	pid_t pid = fork();
	if (pid == -1) {
		return -1;
	} else if (pid == 0) {
		// setpgid(0, 0);
		char* args[] = {shellExe, NULL};
		execv(shellExe, args);
		perror("fail to exec shell");
		exit(1);
	}

	setpgid(pid, pid);
	tcsetpgrp(STDIN_FILENO, pid);

	return pid;
}

static pid_t runCPU(char* cpuExe, bool log) {
	pid_t pid = fork();
	if (pid == -1) {
		return -1;
	} else if (pid == 0) {
		setpgid(0, 0);
		redirectOut("cpu.log");
		char* args[] = {cpuExe, NULL};
		execv(cpuExe, args);
		perror("fail to exec cpu");
		exit(1);
	}

	return pid;
}

int main(int argc, char const* argv[]) {
	initDiagnostics(stdout, "ruemu.debug");

	char* kernimgFilename = NULL;
	char* cpuimg = "iaru0";
	char* shell = "ash";
	bool log = false;

	// Get options
	struct option longOpts[] = {
		{"cpu", optional_argument, 0, 0},
		{"shell", optional_argument, 0, 0},
		{0, 0, 0, 0}
	};

	int opt;
	int optIdx = 0;
	while ((opt = getopt_long(argc, argv, ":l", longOpts, &optIdx)) != -1) {
		switch (opt)	{
			case 'l':
				log = true;
				break;
			case 0:
				if (strcmp("cpu", longOpts[optIdx].name) == 0) cpuimg = optarg;
				else if (strcmp("shell", longOpts[optIdx].name) == 0) shell = optarg;
				break;
			default:
				dLog(D_NONE, DSEV_WARN, "Usage: %s inputfile [--cpu cpuimg] [--shell shell] [-l]", argv[0]);
				break;
		}
	}

	kernimgFilename = (char*) argv[optind];
	if (!kernimgFilename) dFatal(D_ERR_IO, "No kernel image!");

	int shellExists = access(shell, F_OK);
	if (shellExists == -1) dFatal(D_ERR_IO, "No shell binary!");

	int cpuExists = access(cpuimg, F_OK);
	if (cpuExists == -1) dFatal(D_ERR_IO, "No CPU binary!");

	char* ext = strstr(kernimgFilename, ".ark");
	if (!ext) dFatal(D_ERR_IO, "Kernel image does not end in '.ark'!");

	dDebug(DB_BASIC, "Kernel file image is %s", kernimgFilename);

	// Create necessary environment
	dLog(D_NONE, DSEV_INFO, "Creating environment...");
	void* kernimg = loadKernel(kernimgFilename);
	emulatedMemory = createMemory();
	signalsMemory = createSignalMemory();
	setupSignals(signalsMemory);
	dDebug(DB_DETAIL, "Set signals as clean");

	redefineSignal(SIGUSR1, &handleSIGUSR1);
	redefineSignal(SIGSEGV, &handleSIGSEGV);

	// Spawn CPU
	pid_t CPUPID = runCPU(cpuimg, log);
	// Spawn shell
	pid_t shellPID = openShell(shell, log);
	// Shell now takes control of the main stdout/err
	// Emulator is to have its own outstream
	int savedOut = dup(STDOUT_FILENO);
	int savedErr = dup(STDERR_FILENO);
	redirectOut("ruemu.log");

	signalsMemory->metadata.emulatorPID = getpid();
	signalsMemory->metadata.cpuPID = CPUPID;
	signalsMemory->metadata.shellPID = shellPID;

	dLog(D_NONE, DSEV_INFO, "Setting kernel...");
	dDebug(DB_DETAIL, "Start of emulated memory: %p", emulatedMemory);
	setupKernel(emulatedMemory, kernimg, signalsMemory->signals);


	int set = setReadySignal(GET_SIGNAL(signalsMemory->signals, UNIVERSAL_SIG));
	if (set == -1) dFatal(D_ERR_SIGNAL, "No access for ready signal!");
	if (set == 0) dFatal(D_ERR_SIGNAL, "Could not set ready signal!");
	dLog(D_NONE, DSEV_INFO, "Ready signal has been set!");
	dDebug(DB_DETAIL, "Universal signals after setting ready signal: 0x%x", GET_SIGNAL(signalsMemory->signals, UNIVERSAL_SIG)->interrupts);




	int shellStatus, cpuStatus;
	waitpid(shellPID, &shellStatus, 0);
	dLog(D_NONE, DSEV_INFO, "Shell exited with code %d", WEXITSTATUS(shellStatus));
	waitpid(CPUPID, &cpuStatus, 0);
	dLog(D_NONE, DSEV_INFO, "CPU exited with code %d", WEXITSTATUS(cpuStatus));

	dDebug(DB_DETAIL, "Universal interrupts after collecting shell and cpu: 0x%x", GET_SIGNAL(signalsMemory->signals, UNIVERSAL_SIG)->interrupts);


	// Restore it
	dup2(savedOut, STDOUT_FILENO);
	dup2(savedErr, STDERR_FILENO);
	close(savedOut);
	close(savedErr);

	// if (shellStatus == -1) fprintf(stdout, "Shell process ended")
	if (WEXITSTATUS(shellStatus != 0)) dLog(D_NONE, DSEV_WARN, "Shell process ended abnormally. Check the logs.");
	if (WEXITSTATUS(cpuStatus) != 0) dLog(D_NONE, DSEV_WARN, "CPU process ended abnormally. Check the logs.");

	// munmap(kernimg, )
	munmap(emulatedMemory, MEMORY_SPACE_SIZE);
	munmap(signalsMemory->metadata.heap[EMU_HEAP], PAGESIZE);
	munmap(signalsMemory, SIG_MEM_SIZE);
	
	return 0;
}