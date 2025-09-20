#ifndef _HARDWARE_H_
#define _HARDWARE_H_

#include <stdint.h>
#include <stdbool.h>

#include "mem.h"

void alu();
void fpu();
void vcu();
void regfile(bool write);

void imem(uint32_t addr, uint32_t* ival, memerr_t* imemErr);
void dmem(uint32_t addr, uint32_t* rval, uint32_t* wval, memerr_t* imemErr);

#endif