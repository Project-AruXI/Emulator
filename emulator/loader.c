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


#define USER_START 0x20040000 
#define USER_BSS 0x20040000
#define USER_CONST 0x20080000
#define USER_DATA 0x20090000
#define USER_TEXT 0x20190000


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

	write(STDOUT_FILENO, "MMAP'd\n", 7);

	uint8_t* binary = (uint8_t*) ptr;
	AOEFFheader* header = (AOEFFheader*) ptr;
	
	// Check it is an AOEFF and it is type executable
	if (header->hID[AHID_0] != AH_ID0 && header->hID[AHID_1] != AH_ID1 && 
			header->hID[AHID_2] != AH_ID2 && header->hID[AH_ID3] != AH_ID3) dFatal(D_ERR_INVALID_FORMAT, "File is not an AOEFF!");

	if (header->hType != AHT_EXEC) dFatal(D_ERR_INVALID_FORMAT, "File is not an executable!");

	uint32_t entry = header->hEntry;

	// Now that it is mmap'd, load the contents
	write(STDOUT_FILENO, "Checked format, will load\n", 26);

	AOEFFSectHeader* sectHdrs = (AOEFFSectHeader*)(binary + header->hSectOff);
	uint32_t sectHdrsSize = header->hSectSize;

	// Set the data and text information
	for (uint32_t i = 0; i < sectHdrsSize; i++) {
		AOEFFSectHeader* sectHdr = &(sectHdrs[i]);

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

			dDebug(DB_DETAIL, "Start of data section in binary: %p::Start of user data section in emulated memory:%p", binaryData, dataStart);
			dDebug(DB_DETAIL, "First item in data: 0x%x from 0x%x", *(dataStart), *(binaryData));

			memcpy(dataStart, binaryData, sectHdr->shSectSize);
		} else if (strncmp(".text", sectHdr->shSectName, 8) == 0) {
			uint8_t* textStart = memory + USER_TEXT;
			uint8_t* binaryText = binary + sectHdr->shSectOff;

			dDebug(DB_DETAIL, "Start of text section in binary: %p::Start of user text section in emulated memory:%p", binaryText, textStart);
			dDebug(DB_DETAIL, "First item in text: 0x%x from 0x%x", *(textStart), *(binaryText));

			memcpy(textStart, binaryText, sectHdr->shSectSize);
		}
	}

	munmap(ptr, statBuffer.st_size);

	return entry;
}