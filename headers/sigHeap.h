#ifndef _SIGNAL_HEAP_H_
#define _SIGNAL_HEAP_H_

#include <stdbool.h>
#include <stdint.h>

typedef unsigned int sigsize_t;

void sinit(void* _sigHeapPtr, bool new);

void* smalloc(sigsize_t size);

void sfree(void* ptr);




uint32_t ptrToOffset(void* ptr, bool* valid);

void* offsetToPtr(uint32_t offset);

char* sstrdup(const char* src);

#endif