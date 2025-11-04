#ifndef _EMU_LOADER_H_
#define _EMU_LOADED_H_

#include <stdint.h>


typedef struct DynamicLibrarySymbol {
	const char* symbname; // The name of the symbol
	uint32_t vaddr; // The address in emulated memory
	uint8_t* paddr; // The real address in the shared memory
} DyLibSymb;

typedef struct DynamicLibrary {
	const char* libname; // The name of the library
	uint32_t vaddr; // The address in emulated memory that the library is loaded at
	uint8_t* paddr; // The real address in the shared memory that the library is loaded at

	struct {
		DyLibSymb* symbs;
		uint32_t count;
	} symbols;
} DyLib;

/**
 * Used to aid dynamic linking.
 */
typedef struct DynamicLibraryCache {
	DyLib* libs;
	uint32_t count;
	uint32_t cap;
} DyLibCache;


/**
 * Loads the kernel image binary into the emulated memory.
 * @param filename The path to the kernel image file
 * @param memory The emulated memory
 * @return The entry point of the kernel
 */
uint32_t loadKernel(char* filename, uint8_t* memory);

/**
 * Loads the user binary into the emulated memory.
 * Checks that any shared libraries required are loaded.
 * @param filename The path to the user program file
 * @param memory The emulated memory
 * @return The entry point of the user program
 */
uint32_t loadBinary(char* filename, uint8_t* memory);

/**
 * Loads the default shared libraries into the emulator's memory space.
 * This includes standard libraries like `stdlib`, etc.
 * @param memory The emulated memory
 */
void loadDefaultLibraries(uint8_t* memory);

/**
 * Loads a shared library at loadtime into the emulator's memory space.
 * This is for loading the default libraries (ie stdlib) and user-passed libraries
 *   done via `--libload` at emulator startup.
 * Note that `filename` is just the name of the library file, not a path.
 * When passing it as an argument, the prefix is assumed, so `--libload=lib0` instead of `--libload=lib0.adlib`
 * This will search the default library paths for the library.
 * @param filename The name of the library file
 * @param memory The emulated memory
 * @return 0 if loaded, -1 if could not be found, 1 if other
 */
int loadLibrary(char* filename, uint8_t* memory);

/**
 * Loads a shared library at runtime into the emulator's memory space.
 * This is for loading shared libraries via syscalls (ie `newdl`).
 * Note that `filename` is just the name of the library file, not a path.
 * This will search the default library paths for the library.
 * It will also search any paths set via the shell path environment variable.
 * @param filename The name of the library file
 * @param memory The emulated memory
 */
void loadLibraryRuntime(char* filename, uint8_t* memory);


#endif