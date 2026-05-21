#ifndef SLEEPY_DISASM_H
#define SLEEPY_DISASM_H

#include "sleepy_chunk.h"
#include "sleepy_opcodes.h"
#include <stdio.h>

const char *sleepy_opcode_name(SleepyOpcode op);
int sleepy_disasm_instruction(SleepyChunk *chunk, int offset, FILE *out);
void sleepy_disasm_chunk(SleepyChunk *chunk, const char *name, FILE *out);

#endif // SLEEPY_H
