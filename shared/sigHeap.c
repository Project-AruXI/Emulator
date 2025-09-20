#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "diagnostics.h"
#include "sigHeap.h"


/**
 * Heap Block structure
 * |----4 bytes---|
 * |    CANARY 		|
 * |		 size			|
 * |		flags			|
 * | CANARY|NEXT	|
 * |--------------|
 * | 		PAYLOAD 	|
 * | 		PAYLOAD 	|
 * |--------------|
 * 
 * `size` includes the number of bytes that they payload has
 * it does not take into account the metadata
 * `flags`: [b31, ..., b3, b2, b1, b0]
 * 		As of now, flags only contain whether the block has been allocated, signified in b0
 * 		1 if allocated, 0 if free
 * For allocated blocks, `CANARY|NEXT` will contain a canary value to test for overwritten
 * For free blocks, it contains the offset to the next free block
 * 
 * Note that offsets are used instead of pointers since the heap is shared
 * but the pointers vary between processes. Thus by keeping everything relative to the 
 * beginning (saved per user), data can be transferred and seen across.
 */

typedef struct HeapBlock {
	uint32_t canary0;
	sigsize_t size; // Size of the data block, excluding metadata
	uint32_t flags;
	union {
		uint32_t canary1;
		uint32_t nextOff;
	} addr;
} hblock_t;

#define METADATA_SIZE sizeof(hblock_t)
#define CANARY 0xDAEFFAED
#define _PAGESIZE 4096

#define ALIGNMENT 8
#define ALIGN(x) (((x) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))

#define GET_PTR(off) ((hblock_t*)(signalHeap + off))
#define GET_OFF(ptr) ((char*)(ptr) - signalHeap)

static char* signalHeap;
static hblock_t* START; // The very first (unusable) block in the signal heap


static void displayFreeList() {
	dDebug(DB_DETAIL, "START (%p):\nsize: 0x%x\nnext: %p\n", START, START->size, GET_PTR(START->addr.nextOff));

	uint32_t currOff = START->addr.nextOff;

	while (currOff != 0x00000000 && currOff != CANARY) {
		hblock_t* curr = GET_PTR(currOff);
		dDebug(DB_DETAIL, "Block %p:\ncanary0: 0x%x\nsize: 0x%x\nflags: 0x%x\nnextOff: 0x%x\n",
				curr, curr->canary0, curr->size, curr->flags, curr->addr.nextOff);
		currOff = curr->addr.nextOff;
	}
}

static void setupBlock(hblock_t* block, sigsize_t size, bool alloc) {
	block->canary0 = CANARY;
	block->size = size;
	block->flags = alloc;

	if (alloc) block->addr.canary1 = CANARY;
	else block->addr.nextOff = 0x00000000;
}

/**
 * Finds a free block with a minimum size provided.
 * @param size 
 * @return 
 */
static hblock_t* findBlock(sigsize_t size) {
	hblock_t* currBlock = NULL;
	uint32_t currBlockOff = START->addr.nextOff;
	hblock_t* currBest = NULL;

	sigsize_t currDiff = 0, currMinDiff = UINT32_MAX;

	while (currBlockOff != 0x00000000) {
		currBlock = GET_PTR(currBlockOff);

		// Size difference between the target and actual data size
		// Need to minimize difference
		currDiff = currBlock->size - size;

		if (currBlock->size >= size && currDiff <= currMinDiff) {
			currMinDiff = currDiff;
			currBest = currBlock;
		}

		currBlockOff = currBest->addr.nextOff;
	}

	return currBest;
}

static hblock_t* splitTruncate(hblock_t* block, sigsize_t size) {
	hblock_t* temp = block;
	temp++; // Make temp point to the start of payload data

	char* _tempc = (char*) temp;
	_tempc += size; // Create requested memory
	temp = (hblock_t*) _tempc;
	// temp now at the end of requested memory, possibly at start of new free

	// Get the remaining size of the new free block, includes space for metadata
	sigsize_t newFreeBlockSize = block->size - size;


	// Get the previous block for linking purposes
	hblock_t* prev = START;
	while (GET_PTR(prev->addr.nextOff) != block) prev = GET_PTR(prev->addr.nextOff);

	// If block to allocate takes up everything, cannot split
	// Else, place a new free block
	if (newFreeBlockSize > (METADATA_SIZE+ALIGNMENT)) {
		// Space for a free block
		// That being, space for free block meaning sufficient for metadata
		//   and minimnum 8 bytes

		setupBlock(temp, newFreeBlockSize-METADATA_SIZE, false);

		prev->addr.nextOff = GET_OFF(temp);
		temp->addr.nextOff = block->addr.nextOff;
	} else {
 		// else cannot split, just link
		// Only link previous to next, aka removing from free list
		prev->addr.nextOff = block->addr.nextOff;
	}

	setupBlock(block, size, true);

	return block;
}


void sinit(void* _sigHeapPtr, bool new) {
	signalHeap = _sigHeapPtr;
	START = (hblock_t*) signalHeap;

	if (new) {
		// Set up the entry point and actual heap
		// No data in entry so size is the metadata size itself
		setupBlock(START, METADATA_SIZE, false);
		setupBlock(START + 1, _PAGESIZE - (METADATA_SIZE*2), false);
	}
	// START + 1 is just the first actual free block
	/**
	 * | SIGNAL HEAP |
	 * |-------------|
	 * |    START    |
	 * |-------------|
	 * |  FREE BLOCK | (START + 1)
	 * |- - - - - - -|
	 * |  FREE SPACE |
	 * |-------------|
	 */
	START->addr.nextOff = GET_OFF(START+1);
}

void* smalloc(sigsize_t size) {
	if (size == 0) return NULL;

	// write(STDOUT_FILENO, "smalloc::Will align\n", 20);
	// All payload data is to be in multiples of 8
	size = ALIGN(size);

	// Get a block with sufficient size
	// write(STDOUT_FILENO, "smalloc::Will find block\n", 25);
	hblock_t* block = findBlock(size);
	if (!block) return NULL;

	// Have the acquired block be reduced to the necessary limit
	// And create a new free block
	// Note that this is important on the first allocation as there is only one block
	//  and it would not be good to give the user the entire heap :(
	// write(STDOUT_FILENO, "smalloc::Will trunctate\n", 24);
	block = splitTruncate(block, size);

	// Return the start of the actual memory block
	void* retblock = (void*)(block + 1);
	// write(STDOUT_FILENO, "smalloc::Returning\n", 19);
	return retblock;
}

void sfree(void* ptr) {
	if (!ptr) return;

	// Pointer was given for payload, step back to the actual beginning
	hblock_t* blockToFree = ((hblock_t*)ptr) - 1;

	// Check double free
	if (blockToFree->flags == 0b0) return;

	// Check overwritten metadata
	if (blockToFree->canary0 != CANARY && blockToFree->addr.canary1 != CANARY) return;

	// TODO: Better error indicating for memory freeing

	// Re-link, aka place it back in free list
	hblock_t* curr = GET_PTR(START->addr.nextOff);
	uint32_t temp = 0x00000000;

	// Iterate until next block of current is past the block to free
	while (curr->addr.nextOff != 0x00000000 && (GET_PTR(curr->addr.nextOff) < blockToFree)) curr = GET_PTR(curr->addr.nextOff);

	temp = curr->addr.nextOff;

	curr->addr.nextOff = GET_OFF(blockToFree);
	blockToFree->addr.nextOff = temp;

	blockToFree->flags = 0b0;

	// merge(blockToFree);
}


uint32_t ptrToOffset(void* ptr, bool* valid) {
	// Make sure the pointer is within bounds
	if (ptr <= (void*)signalHeap || ptr >= (void*)(signalHeap+_PAGESIZE)) *valid = false;

	// Offset is from the start of the heap to where the pointer is at
	uint32_t offset = ((char*) ptr) - ((char*) signalHeap);

	return offset;
}

void* offsetToPtr(uint32_t offset) {

	char* ptr = ((char*) signalHeap) + offset;

	return (void*) ptr;
}


char* sstrdup(const char* src) {
	size_t len = strlen(src) + 1;
	char* dest = smalloc(len);
	if (!dest) return NULL;
	
	strcpy(dest, src);

	return dest;
}