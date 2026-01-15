#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "aoef.h"
#include "loader.h"
#include "diagnostics.h"



static void applyRelocation(uint32_t* location, uint8_t type, uint32_t symbValue, int32_t addend) {
	dLog(D_NONE, DSEV_INFO, "Applying relocation of type 0x%x at location 0x%p with symbol value 0x%x and addend 0x%x", type, location, symbValue, addend);

	switch (type) {
		case RE_ARU32_DECOMP:
			uint32_t fullNumber = symbValue + addend;

			uint16_t high14 = (fullNumber >> 18) & 0x3FFF; // Get bits 18-31
			uint16_t mid14 = (fullNumber >> 4) & 0x3FFF; // Get bits 4-17
			uint8_t low4 = fullNumber & 0xF; // Get bits 0-3

			*location &= ~(0x3FFF << 10); // Clear bits 10-23
			*location |= (high14 << 10);

			uint32_t* midLocation = location + 2;
			*midLocation &= ~(0x3FFF << 10); // Clear bits 10-23
			*midLocation |= (mid14 << 10);

			uint32_t* lowLocation = location + 5;
			*lowLocation &= ~(0xF << 10); // Clear bits 10-13
			*lowLocation |= (low4 << 10);
			break;
		default:
			break;
	}
}

static void relocateFJT(AOEFFDRelTab* relTab, uint8_t* binary, uint8_t* memory, uint32_t vAddr, DyLibCache* libCache) {
	dDebug(DB_BASIC, "relocateFJT::Relocating FJT table...");

	AOEFFDRelEnt* relEntries = (AOEFFDRelEnt*)(&relTab->relEntries);
	uint32_t relEntryCount = relTab->relCount;

	AOEFFhdr* header = (AOEFFhdr*) binary;
	AOEFFSymEnt* symbTab = (AOEFFSymEnt*)(binary + header->hSymbOff);
	AOEFFImportEnt* importTab = (AOEFFImportEnt*)(binary + header->hImportTabOff);
	uint32_t importTabSize = header->hImportTabSize;
	AOEFFDyLibEnt* dylibTab = (AOEFFDyLibEnt*)(binary + header->hDyLibTabOff);

	for (uint32_t i = 0; i < relEntryCount; i++) {
		AOEFFDRelEnt* relEntry = &relEntries[i];
		// Relocate this entry

		uint32_t* location = (uint32_t*)(memory + relEntry->reOff);
		uint32_t relSymbIndex = relEntry->reSymb; // The symbol table index of the symbol to relocate against

		char* symbName = (char*)(binary + header->hStrTabOff + symbTab[relSymbIndex].seSymbName);
		dDebug(DB_DETAIL, "Relocating symbol `%s` at location 0x%p", symbName, location);

		// Need to find the value of the symbol
		// This is found in the library it is imported from
		// Use the import table to find which library it is from
		// Then use the dynamic library cache to find the symbol value

		// Get the import entry with matching symbol index
		AOEFFImportEnt* importEntry = NULL;
		for (uint32_t j = 0; j < importTabSize; j++) {
			AOEFFImportEnt* currentImportEntry = &importTab[j];
			if (currentImportEntry->ieSymb == relSymbIndex) {
				importEntry = currentImportEntry;
				break;
			}
		}

		uint32_t importLibIndex = importEntry->ieDyLib; // The dynamic library index of the library this symbol is imported from

		AOEFFDyLibEnt importLib = dylibTab[importLibIndex]; // The dynamic library entry this symbol is imported from

		// At this point, we have the dynamic library of the symbol to relocate against
		// Now, search the dynamic library cache for this library to get the value of the symbol
		char* importLibName = (char*)(binary + header->hDyLibStrTabOff + importLib.dlName);
		uint32_t symbolValue = 0;
		bool symbolFound = false;
		for (uint32_t k = 0; k < libCache->count; k++) {
			DyLib currentLib = libCache->libs[k];
			if (strcmp(currentLib.libname, importLibName) == 0) {
				// Found the library, now search for the symbol
				for (uint32_t l = 0; l < currentLib.symbols.count; l++) {
					DyLibSymb currentSymb = currentLib.symbols.symbs[l];
					AOEFFSymEnt symbEnt = symbTab[relSymbIndex];
					char* symbName = (char*)(binary + header->hStrTabOff + symbEnt.seSymbName);
					if (strcmp(currentSymb.symbname, symbName) == 0) {
						// Found the symbol
						symbolValue = currentSymb.symbval;
						symbolFound = true;
						break;
					}
				}
			}
		}
		if (!symbolFound) {
			dLog(D_ERR_DLIB, DSEV_WARN, "Could not find symbol %s in linked libraries to relocate.", symbName);
		}

		applyRelocation(location, relEntries->reType, symbolValue, relEntries->reAddend);
	}
}


void relocate(uint8_t* binary, uint8_t* memory, uint32_t vAddr, DyLibCache* libCache) {
	dLog(D_NONE, DSEV_INFO, "Starting relocation process.");

	AOEFFhdr* header = (AOEFFhdr*) binary;

	AOEFFDRelTab* relTab = (AOEFFDRelTab*)(binary + header->hDRelTabOff);
	uint32_t relTabCount = header->hDRelTabSize;

	char* relStrTab = (char*)(binary + header->hRelStrTabOff);

	// Go through all the tables
	// For each table, go through all the entries and relocate
	for (uint32_t i = 0; i < relTabCount; i++) {
		AOEFFDRelTab currentRelTab = relTab[i];
		
		char* tableName = relStrTab + currentRelTab.relTabName;
		dDebug(DB_BASIC, "Relocating table %d (%s)...", i, tableName);
		if (strcmp(tableName, ".drel.fjt")) {
			relocateFJT(&currentRelTab, binary, memory, vAddr, libCache);
		}
	}
}