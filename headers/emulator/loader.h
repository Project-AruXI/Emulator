#ifndef _EMU_LOADER_H_
#define _EMU_LOADED_H_

#include <stdint.h>




typedef struct DyLibCache {

};

uint32_t loadKernel(char* filename, uint8_t* memory);

uint32_t loadBinary(char* filename, uint8_t* memory);

/**
 * Loads a shared library at runtime into the emulator's memory space.
 * 
 */
void loadLibraryRuntime(char* filename, uint8_t* memory);


#endif