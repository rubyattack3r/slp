#ifndef SLP_DISASM_H
#define SLP_DISASM_H

#include "slp_chunk.h"
#include "slp_opcodes.h"
#include <stdio.h>

const char *slp_opcode_name(SlpOpcode op);
int slp_disasm_instruction(SlpChunk *chunk, int offset, FILE *out);
void slp_disasm_chunk(SlpChunk *chunk, const char *name, FILE *out);

#endif // SLP_H
