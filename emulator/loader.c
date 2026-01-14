#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "loader.h"
#include "emSignal.h"
#include "aoef.h"
#include "diagnostics.h"
#include "shmem.h"
#include "memsects.h"


DyLibCache dylibCache;

static void addLibToCache(DyLibCache* cache, char* libname, uint32_t vaddr, uint8_t* paddr, DyLibSymb* symbs, uint32_t symbCount) {
	// For now, the cache will be linear
	// But since search time will be O(n^2) to search for dylib name then symbol name, improvements need to be made
	// Maybe via hashing

	if (cache->count == cache->cap) {
		uint32_t newCap = (cache->cap == 0) ? 4 : cache->cap * 2;
		DyLib* newLibs = (DyLib*) realloc(cache->libs, sizeof(DyLib) * newCap);
		if (!newLibs) dFatal(D_ERR_MEM, "Could not allocate memory for dynamic library cache!");

		cache->libs = newLibs;
		cache->cap = newCap;
	}

	DyLib* dylib = &cache->libs[cache->count++];
	dylib->libname = strdup(libname);
	dylib->vaddr = vaddr;
	dylib->paddr = paddr;
	// dylib->symbols.symbs = symbs;
	// dylib->symbols.count = symbCount;
}


uint32_t loadKernel(char* filename, uint8_t* memory) {
	int fd = open(filename, O_RDONLY);
	if (fd < 0) dFatal(D_ERR_IO, "Could not open kernel image %s!", filename);

	struct stat statBuffer;
	int rc = fstat(fd, &statBuffer);
	if (rc != 0) dFatal(D_ERR_IO, "Could not stat file descriptor!");

	void* ptr = mmap(0, statBuffer.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (ptr == MAP_FAILED) dFatal(D_ERR_INTERNAL, "Could not map file!");
	close(fd);

	uint8_t* kernimg = (uint8_t*) ptr;
	AOEFFhdr* header = (AOEFFhdr*) ptr;

	// Check it is an AOEFF and it is type kernel
	if (header->hID[AHID_0] != AH_ID0 && header->hID[AHID_1] != AH_ID1 && 
			header->hID[AHID_2] != AH_ID2 && header->hID[AH_ID3] != AH_ID3) dFatal(D_ERR_INVALID_FORMAT, "File is not an AOEFF!");

	if (header->hType != AHT_KERN) dFatal(D_ERR_INVALID_FORMAT, "File is not a kernel image!");

	uint32_t kernEntry = header->hEntry;

	AOEFFSectHdr* sectHdrs = (AOEFFSectHdr*)(kernimg + header->hSectOff);
	uint32_t sectHdrsSize = header->hSectSize;

	// Set the data and text information
	for (uint32_t i = 0; i < sectHdrsSize; i++) {
		AOEFFSectHdr* sectHdr = &(sectHdrs[i]);

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

	return kernEntry;
}

static void loadLinkedLibraries(uint8_t* binary, uint8_t* memory) {
	AOEFFhdr* header = (AOEFFhdr*) binary;

	// Load dynamic libraries
	AOEFFDyLibEnt* dylibEntries = (AOEFFDyLibEnt*)(binary + header->hDyLibTabOff);

	for (uint32_t i = 0; i < header->hDyLibTabSize; i++) {
		AOEFFDyLibEnt* dylibEntry = &dylibEntries[i];

		// Get the name of the library
		char* _dylibName = (char*)(binary + header->hDyLibStrTabOff + dylibEntry->dlName);
		// The stored named as the .adlib extension, but when loading, it is assumed, need to remove it
		char* dylibName = strdup(_dylibName);
		char* extPos = strstr(dylibName, ".adlib");
		if (extPos) *extPos = '\0';

		dDebug(DB_BASIC, "Loading dynamic library dependency: %s", dylibName);

		if (loadLibrary(dylibName, memory) != 0) dFatal(D_ERR_DLIB, "Could not load dynamic library dependency %s!", dylibName);
	}
}

uint32_t loadBinary(char* filename, uint8_t* memory) {
	int fd = open(filename, O_RDONLY);
	if (fd < 0) dFatal(D_ERR_IO, "Could not open program 0x`%x` (@`%p`)!", filename, filename);

	struct stat statBuffer;
	int rc = fstat(fd, &statBuffer);
	if (rc != 0) {
		write(STDERR_FILENO, "Could not stat file descriptor!", 31);
		dFatal(D_ERR_IO, "Could not stat file descriptor!");
	}

	void* ptr = mmap(0, statBuffer.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (ptr == MAP_FAILED) {
		write(STDERR_FILENO, "Could not map file\n", 19);
		dFatal(D_ERR_INTERNAL, "Could not map file!");
	}
	close(fd);

	// write(STDOUT_FILENO, "MMAP'd\n", 7);

	uint8_t* binary = (uint8_t*) ptr;
	AOEFFhdr* header = (AOEFFhdr*) ptr;

	// Check it is an AOEFF and it is type executable
	if (header->hID[AHID_0] != AH_ID0 && header->hID[AHID_1] != AH_ID1 && 
			header->hID[AHID_2] != AH_ID2 && header->hID[AH_ID3] != AH_ID3) dFatal(D_ERR_INVALID_FORMAT, "File is not an AOEFF!");

	if (header->hType != AHT_EXEC) dFatal(D_ERR_INVALID_FORMAT, "File is not an executable!");

	uint32_t entry = header->hEntry;


	// Check if it has any library dependencies, if so, load them
	// No need to check if symbols are present, that was done by the linker
	loadLinkedLibraries(binary, memory);

	// Now that it is mmap'd, load the contents
	write(STDOUT_FILENO, "Checked format, will load\n", 26);

	AOEFFSectHdr* sectHdrs = (AOEFFSectHdr*)(binary + header->hSectOff);
	uint32_t sectHdrsSize = header->hSectSize;

	// Set the data and text information
	uint32_t fjtOffset = 0x0;
	uint32_t djtOffset = 0x0;
	for (uint32_t i = 0; i < sectHdrsSize; i++) {
		AOEFFSectHdr* sectHdr = &(sectHdrs[i]);

		// Make sure size of each section does not exceed allowed size in memory
		// Except considering the size allocated, no need, at least for now
		// But when checking is to be done, a way to indicate the program cannot be loaded
		// and have it be propogated back to the shell must be done

		if (strncmp(".bss", sectHdr->shSectName, 8) == 0) {
			// When the emulator creates the shared memory then ftruncates it, everything is set to 0
			// Meaning all of the bss (256KB) is zero'd out, no need to get the size of the section
			// Unless the size is greater than that permitted)
		} else if (strncmp(".const", sectHdr->shSectName, 8) == 0) {
			uint8_t* constStart = memory + USER_TEXT;
			uint8_t* binaryConst = binary + sectHdr->shSectOff;

			dDebug(DB_DETAIL, "Start of const section in binary: %p::Start of user const section in emulated memory:%p", binaryConst, constStart);
			dDebug(DB_DETAIL, "First item in const: 0x%x from 0x%x", *(constStart), *(binaryConst));

			memcpy(constStart, binaryConst, sectHdr->shSectSize);
		} else if (strncmp(".data", sectHdr->shSectName, 8) == 0) {
			uint8_t* dataStart = memory + USER_TEXT;
			uint8_t* binaryData = binary + sectHdr->shSectOff;

			djtOffset = sectHdr->shSectOff + sectHdr->shSectSize;

			dDebug(DB_DETAIL, "Start of data section in binary: %p::Start of user data section in emulated memory:%p", binaryData, dataStart);
			dDebug(DB_DETAIL, "First item in data: 0x%x from 0x%x", *(dataStart), *(binaryData));

			memcpy(dataStart, binaryData, sectHdr->shSectSize);
		} else if (strncmp(".text", sectHdr->shSectName, 8) == 0) {
			uint8_t* textStart = memory + USER_TEXT;
			uint8_t* binaryText = binary + sectHdr->shSectOff;

			fjtOffset = sectHdr->shSectOff + sectHdr->shSectSize;

			dDebug(DB_DETAIL, "Start of text section in binary: %p::Start of user text section in emulated memory:%p", binaryText, textStart);
			dDebug(DB_DETAIL, "First item in text: 0x%x from 0x%x", *(textStart), *(binaryText));

			memcpy(textStart, binaryText, sectHdr->shSectSize);
		} else if (strncmp(".fjt", sectHdr->shSectName, 8) == 0) {
			uint8_t* fjtStart = memory + USER_TEXT + fjtOffset;
			uint8_t* binaryFjt = binary + sectHdr->shSectOff;

			dDebug(DB_DETAIL, "Start of fjt section in binary: %p::Start of user fjt section in emulated memory:%p", binaryFjt, fjtStart);
			dDebug(DB_DETAIL, "First item in fjt: 0x%x from 0x%x", *(fjtStart), *(binaryFjt));

			memcpy(fjtStart, binaryFjt, sectHdr->shSectSize);
		} else if (strncmp(".djt", sectHdr->shSectName, 8) == 0) {
			uint8_t* djtStart = memory + USER_TEXT + djtOffset;
			uint8_t* binaryDjt = binary + sectHdr->shSectOff;

			dDebug(DB_DETAIL, "Start of djt section in binary: %p::Start of user djt section in emulated memory:%p", binaryDjt, djtStart);
			dDebug(DB_DETAIL, "First item in djt: 0x%x from 0x%x", *(djtStart), *(binaryDjt));

			memcpy(djtStart, binaryDjt, sectHdr->shSectSize);
		}
	}

	relocate(binary, memory, USER_START);

	munmap(ptr, statBuffer.st_size);

	return entry;
}

void loadDefaultLibraries(uint8_t* memory) {
	// Just a wrapper around loadLibrary

	char* defaultLibs[] = {
		"std",
	};

	for (int i = 0; i < 1; i++) {
		if (loadLibrary(defaultLibs[i], memory) == -1) {
			// Defaults could not be found, quit
			dFatal(D_ERR_DLIB, "Default mandatory library cannot be loaded. Ensure the library is present.");
		}
	}
}

static bool loadLibBinary(char* path, char* libname, uint8_t* memory) {
	int fd = open(path, O_RDONLY);
	if (fd < 0) return false;

	struct stat statBuffer;
	int rc = fstat(fd, &statBuffer);
	if (rc != 0) {
		write(STDERR_FILENO, "Could not stat file descriptor!", 31);
		dFatal(D_ERR_IO, "Could not stat file descriptor!");
	}

	void* ptr = mmap(0, statBuffer.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (ptr == MAP_FAILED) {
		write(STDERR_FILENO, "Could not map file\n", 19);
		dFatal(D_ERR_INTERNAL, "Could not map file!");
	}
	close(fd);

	write(STDOUT_FILENO, "MMAP'd\n", 7);

	uint8_t* binary = (uint8_t*) ptr;
	AOEFFhdr* header = (AOEFFhdr*) ptr;

	// Check it is an AOEFF and it is type library
	if (header->hID[AHID_0] != AH_ID0 && header->hID[AHID_1] != AH_ID1 && 
			header->hID[AHID_2] != AH_ID2 && header->hID[AH_ID3] != AH_ID3) dFatal(D_ERR_INVALID_FORMAT, "File is not an AOEFF!");

	if (header->hType != AHT_DLIB) dFatal(D_ERR_INVALID_FORMAT, "File is not a dynamic library!");

	// The start vaddr of this library is the start of the previous library + size of previous library
	// If no current libraries, start at the default library area
	uint32_t libstartVAddr = SYS_LIB; // Where the library is loaded in the emulated memory
	uint8_t* libstartPAddr = memory + SYS_LIB; // The real address in shared memory
	if (dylibCache.count != 0) {
		DyLib* prevLib = &dylibCache.libs[dylibCache.count - 1];
		libstartVAddr = prevLib->vaddr + prevLib->size;
		libstartPAddr = prevLib->paddr + prevLib->size;
	}


	DyLibSymb* symbs = (DyLibSymb*) malloc(sizeof(DyLibSymb) * header->hSymbSize);
	if (!symbs) dFatal(D_ERR_MEM, "Could not allocate memory for dynamic library cache symbols.");

	AOEFFExportEnt* exportSymbols = (AOEFFExportEnt*)(binary + header->hExportTabOff);
	char* strTab = (char*)(binary + header->hStrTabOff);
	AOEFFSymEnt* symbTab = (AOEFFSymEnt*)(binary + header->hSymbOff);

	for (uint32_t i = 0; i < header->hExportTabSize; i++) {
		AOEFFExportEnt exportEnt = exportSymbols[i];
		AOEFFSymEnt symbEnt = symbTab[exportEnt.eeSymb];

		char* symbName = strTab + symbEnt.seSymbName;
		uint32_t symbVal = libstartVAddr + exportEnt.eeAddress; // The value is the updated address in emulated memory
		// eeAddress gives the offset from the start of the library (aka from 0x0)
		// It needs to be updated to the actual address in emulated memory

		symbs[i].symbname = strdup(symbName);
		symbs[i].symbval = symbVal;

		dDebug(DB_DETAIL, "Saved symbol `%s` with value 0x%x to cache", symbName, symbVal);
	}

	addLibToCache(&dylibCache, libname, libstartVAddr, libstartPAddr, symbs, header->hSymbSize);

	// Place the binary in the emulated memory
	// Refer to the documentation of the layout

	AOEFFSectHdr* sectHdrs = (AOEFFSectHdr*)(binary + header->hSectOff);

	uint32_t currentSectionOffset = libstartVAddr; // The offset (in emulated memory) of the current section being loaded

	uint8_t* libBinaryBss = NULL;
	uint8_t* libBinaryConst = NULL;
	uint8_t* libBinaryData = NULL;
	uint8_t* libBinaryText = NULL;
	for (uint32_t i = 0; i < header->hSectSize; i++) {
		AOEFFSectHdr sectHdr = sectHdrs[i];
		
		if (sectHdr.shSectName[1] == 'b' && sectHdr.shSectSize != 0) libBinaryBss = binary + sectHdr.shSectOff;
		else if (sectHdr.shSectName[1] == 'c' && sectHdr.shSectSize != 0) libBinaryConst = binary + sectHdr.shSectOff;
		else if (sectHdr.shSectName[1] == 'd' && sectHdr.shSectSize != 0) libBinaryData = binary + sectHdr.shSectOff;
		else if (sectHdr.shSectName[1] == 't' && sectHdr.shSectSize != 0) libBinaryText = binary + sectHdr.shSectOff;
	}

	// Copy sections in order: bss, const, data, text
	if (libBinaryBss) {
		currentSectionOffset += sectHdrs[0].shSectSize;
	}
	if (libBinaryConst) {
		uint8_t* sectStart = memory + currentSectionOffset;
		memcpy(sectStart, libBinaryConst, sectHdrs[1].shSectSize);
		currentSectionOffset += sectHdrs[1].shSectSize;
	}
	if (libBinaryData) {
		uint8_t* sectStart = memory + currentSectionOffset;
		memcpy(sectStart, libBinaryData, sectHdrs[2].shSectSize);
		currentSectionOffset += sectHdrs[2].shSectSize;
	}
	if (libBinaryText) {
		uint8_t* sectStart = memory + currentSectionOffset;
		memcpy(sectStart, libBinaryText, sectHdrs[3].shSectSize);
		currentSectionOffset += sectHdrs[3].shSectSize;
	}

	// relocate(binary, memory, libstartVAddr);

	dLog(D_NONE, DSEV_INFO, "Loaded library `%s` at 0x%x (%p)", libname, libstartVAddr, libstartPAddr);

	munmap(ptr, statBuffer.st_size);

	return true;
}

int loadLibrary(char* filename, uint8_t* memory) {
	if (strstr(filename, ".adlib")) {
		dLog(D_NONE, DSEV_WARN, "Extension is assumed. Ignoring.");
		return 1;
	}

	char* defaultLibPaths[] = {
		".", // For now, have the current directory be the default path
	};

	for (int i = 0; i < 1; i++) {
		char* libpath = defaultLibPaths[i];

		char* fullPath = (char*) malloc(sizeof(char) * strlen(libpath) + strlen(filename) + 8);
		if (!fullPath) dFatal(D_ERR_MEM, "Could not allocate memory for path.");

		sprintf(fullPath, "%s/%s.adlib", libpath, filename);

		if (loadLibBinary(fullPath, filename, memory)) {
			free(fullPath);
			return 0;
		}

		free(fullPath);
	}

	dLog(D_NONE, DSEV_WARN, "Library `%s.adlib` could not be found.", filename);
	return -1;
}

void loadLibraryRuntime(char* filename, uint8_t* memory) {
}