#include "slp_disasm.h"
#include "slp_value.h"
#include <stdio.h>

const char *slp_opcode_name(SlpOpcode op) {
    switch (op) {
    case OP_PUSH_NULL:   return "PUSH_NULL";
    case OP_PUSH_TRUE:   return "PUSH_TRUE";
    case OP_PUSH_FALSE:  return "PUSH_FALSE";
    case OP_PUSH_CONST:  return "PUSH_CONST";
    case OP_LOAD_LOCAL_0: return "LOAD_LOCAL_0";
    case OP_LOAD_LOCAL_1: return "LOAD_LOCAL_1";
    case OP_LOAD_LOCAL_2: return "LOAD_LOCAL_2";
    case OP_LOAD_LOCAL_3: return "LOAD_LOCAL_3";
    case OP_LOAD_LOCAL_4: return "LOAD_LOCAL_4";
    case OP_LOAD_LOCAL_5: return "LOAD_LOCAL_5";
    case OP_LOAD_LOCAL_6: return "LOAD_LOCAL_6";
    case OP_LOAD_LOCAL_7: return "LOAD_LOCAL_7";
    case OP_STORE_LOCAL_0: return "STORE_LOCAL_0";
    case OP_STORE_LOCAL_1: return "STORE_LOCAL_1";
    case OP_STORE_LOCAL_2: return "STORE_LOCAL_2";
    case OP_STORE_LOCAL_3: return "STORE_LOCAL_3";
    case OP_STORE_LOCAL_4: return "STORE_LOCAL_4";
    case OP_STORE_LOCAL_5: return "STORE_LOCAL_5";
    case OP_STORE_LOCAL_6: return "STORE_LOCAL_6";
    case OP_STORE_LOCAL_7: return "STORE_LOCAL_7";
    case OP_LOAD_LOCAL:  return "LOAD_LOCAL";
    case OP_STORE_LOCAL: return "STORE_LOCAL";
    case OP_LOAD_GLOBAL: return "LOAD_GLOBAL";
    case OP_STORE_GLOBAL: return "STORE_GLOBAL";
    case OP_LOAD_UPVALUE: return "LOAD_UPVALUE";
    case OP_STORE_UPVALUE: return "STORE_UPVALUE";
    case OP_ADD:         return "ADD";
    case OP_SUBTRACT:    return "SUBTRACT";
    case OP_MULTIPLY:    return "MULTIPLY";
    case OP_DIVIDE:      return "DIVIDE";
    case OP_MODULO:      return "MODULO";
    case OP_POWER:       return "POWER";
    case OP_NEGATE:      return "NEGATE";
    case OP_CONCAT:      return "CONCAT";
    case OP_REPEAT:      return "REPEAT";
    case OP_EQUAL:       return "EQUAL";
    case OP_NOT_EQUAL:   return "NOT_EQUAL";
    case OP_LESS:        return "LESS";
    case OP_GREATER:     return "GREATER";
    case OP_LESS_EQUAL:  return "LESS_EQUAL";
    case OP_GREATER_EQUAL: return "GREATER_EQUAL";
    case OP_SPACESHIP:   return "SPACESHIP";
    case OP_MATCH:       return "MATCH";
    case OP_NOT_MATCH:   return "NOT_MATCH";
    case OP_NOT:         return "NOT";
    case OP_AND:         return "AND";
    case OP_OR:          return "OR";
    case OP_BIT_AND:     return "BIT_AND";
    case OP_BIT_OR:      return "BIT_OR";
    case OP_BIT_XOR:     return "BIT_XOR";
    case OP_BIT_NOT:     return "BIT_NOT";
    case OP_LSHIFT:      return "LSHIFT";
    case OP_RSHIFT:      return "RSHIFT";
    case OP_INCREMENT:   return "INCREMENT";
    case OP_DECREMENT:   return "DECREMENT";
    case OP_UNARY_PREDICATE: return "UNARY_PREDICATE";
    case OP_BINARY_PREDICATE: return "BINARY_PREDICATE";
    case OP_NEGATED_BINARY_PREDICATE: return "NEGATED_BINARY_PREDICATE";
    case OP_POP:         return "POP";
    case OP_DUP:         return "DUP";
    case OP_JUMP:        return "JUMP";
    case OP_JUMP_IF_FALSE: return "JUMP_IF_FALSE";
    case OP_JUMP_IF_TRUE: return "JUMP_IF_TRUE";
    case OP_LOOP:        return "LOOP";
    case OP_CALL:        return "CALL";
    case OP_RETURN:      return "RETURN";
    case OP_CLOSURE:     return "CLOSURE";
    case OP_CLOSE_UPVALUE: return "CLOSE_UPVALUE";
    case OP_FOREACH_NEXT: return "FOREACH_NEXT";
    case OP_BUILD_ARRAY: return "BUILD_ARRAY";
    case OP_BUILD_HASH:  return "BUILD_HASH";
    case OP_INDEX_GET:   return "INDEX_GET";
    case OP_INDEX_SET:   return "INDEX_SET";
    case OP_OBJ_EXPR:    return "OBJ_EXPR";
    case OP_PUSH_HANDLER: return "PUSH_HANDLER";
    case OP_POP_HANDLER: return "POP_HANDLER";
    case OP_THROW:       return "THROW";
    case OP_ASSERT:      return "ASSERT";
    case OP_HALT:        return "HALT";
    case OP_DONE:        return "DONE";
    case OP_YIELD:       return "YIELD";
    case OP_CALLCC:      return "CALLCC";
    case OP_RESUME:      return "RESUME";
    case OP_BRIDGE_REGISTER: return "BRIDGE_REGISTER";
    case OP_IMPORT:      return "IMPORT";
    case OP_BACKTICK:    return "BACKTICK";
    case OP_CLASS_LITERAL: return "CLASS_LITERAL";
    case OP_ADDRESS:     return "ADDRESS";
    case OP_LOCAL:       return "LOCAL";
    case OP_THIS:        return "THIS";
    case OP_UNPACK_TUPLE: return "UNPACK_TUPLE";
    case OP_BREAK:       return "BREAK";
    case OP_CONTINUE:    return "CONTINUE";
    case OP_NOP:         return "NOP";
    case OP_END:         return "END";
    default:             return "UNKNOWN";
    }
}

static void print_value(SlpValue val, FILE *out) {
    if (SLP_IS_NULL(val))       fprintf(out, "null");
    else if (SLP_IS_BOOL(val))  fprintf(out, SLP_AS_BOOL(val) ? "true" : "false");
    else if (SLP_IS_NUM(val))   fprintf(out, "%g", SLP_AS_NUM(val));
    else if (SLP_IS_OBJ(val)) {
        SlpObj *obj = SLP_AS_OBJ(val);
        switch (obj->type) {
        case SLP_OBJ_STRING:
            fprintf(out, "\"%s\"", ((SlpObjString*)obj)->chars);
            break;
        case SLP_OBJ_LONG:
            fprintf(out, "%ldL", (long)((SlpObjLong*)obj)->value);
            break;
        case SLP_OBJ_FUNCTION:
            fprintf(out, "<fn %s>", ((SlpObjFunction*)obj)->name
                ? ((SlpObjFunction*)obj)->name->chars : "(script)");
            break;
        case SLP_OBJ_CLOSURE:
            fprintf(out, "<closure>");
            break;
        case SLP_OBJ_NATIVE:
            fprintf(out, "<native %s>", ((SlpObjNative*)obj)->name
                ? ((SlpObjNative*)obj)->name->chars : "?");
            break;
        default:
            fprintf(out, "<obj %d>", obj->type);
            break;
        }
    } else {
        fprintf(out, "?");
    }
}

static int is_short_instruction(uint8_t op) {
    switch (op) {
    case OP_PUSH_CONST:
    case OP_LOAD_LOCAL:
    case OP_STORE_LOCAL:
    case OP_LOAD_GLOBAL:
    case OP_STORE_GLOBAL:
    case OP_LOAD_UPVALUE:
    case OP_STORE_UPVALUE:
    case OP_JUMP:
    case OP_JUMP_IF_FALSE:
    case OP_JUMP_IF_TRUE:
    case OP_LOOP:
    case OP_AND:
    case OP_OR:
    case OP_PUSH_HANDLER:
    case OP_FOREACH_NEXT:
    case OP_BUILD_ARRAY:
    case OP_BUILD_HASH:
    case OP_OBJ_EXPR:
    case OP_BRIDGE_REGISTER:
    case OP_IMPORT:
    case OP_BACKTICK:
    case OP_CLASS_LITERAL:
        return 1;
    default:
        return 0;
    }
}

int slp_disasm_instruction(SlpChunk *chunk, int offset, FILE *out) {
    fprintf(out, "%04d ", offset);
    if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1])
        fprintf(out, "  | ");
    else
        fprintf(out, "%4d ", chunk->lines[offset]);

    uint8_t instruction = chunk->code[offset];
    const char *name = slp_opcode_name((SlpOpcode)instruction);
    fprintf(out, "%-24s", name);

    if (instruction == OP_PUSH_CONST || instruction == OP_LOAD_GLOBAL ||
        instruction == OP_STORE_GLOBAL) {
        uint16_t idx = slp_chunk_read_short(chunk, offset + 1);
        fprintf(out, " %4d '", idx);
        if (idx < (uint16_t)chunk->constant_count)
            print_value(chunk->constants[idx], out);
        else
            fprintf(out, "???");
        fprintf(out, "'");
        return offset + 3;
    }

    if (instruction == OP_LOAD_LOCAL || instruction == OP_STORE_LOCAL ||
        instruction == OP_LOAD_UPVALUE || instruction == OP_STORE_UPVALUE) {
        // These ops carry a single-byte slot operand (the VM uses read_byte),
        // so read one byte and advance by two; reading a short here would
        // desync the entire disassembly stream.
        uint8_t idx = chunk->code[offset + 1];
        fprintf(out, " %4d", idx);
        return offset + 2;
    }

    if (is_short_instruction(instruction)) {
        uint16_t val = slp_chunk_read_short(chunk, offset + 1);
        if (instruction == OP_JUMP || instruction == OP_JUMP_IF_FALSE ||
            instruction == OP_JUMP_IF_TRUE || instruction == OP_LOOP ||
            instruction == OP_AND || instruction == OP_OR ||
            instruction == OP_PUSH_HANDLER) {
            int sign = (instruction == OP_LOOP) ? -1 : 1;
            fprintf(out, " -> %d", offset + 3 + sign * (int)val);
        } else {
            fprintf(out, " %4d", val);
        }
        return offset + 3;
    }

    if (instruction == OP_CALL) {
        fprintf(out, " %4d", chunk->code[offset + 1]);
        return offset + 2;
    }

    if (instruction == OP_CLOSURE) {
        uint16_t idx = slp_chunk_read_short(chunk, offset + 1);
        fprintf(out, " %4d '", idx);
        if (idx < (uint16_t)chunk->constant_count)
            print_value(chunk->constants[idx], out);
        fprintf(out, "'");
        SlpObjFunction *fn = NULL;
        if (idx < (uint16_t)chunk->constant_count && SLP_IS_OBJ(chunk->constants[idx])
            && SLP_OBJ_TYPE(chunk->constants[idx]) == SLP_OBJ_FUNCTION) {
            fn = (SlpObjFunction*)SLP_AS_OBJ(chunk->constants[idx]);
        }
        int pos = offset + 3;
        if (fn) {
            for (int i = 0; i < fn->upvalue_count; i++) {
                fprintf(out, "\n%04d %4d %-24s %4d %s",
                    pos, chunk->lines[pos], "", chunk->code[pos + 1],
                    chunk->code[pos] ? "upvalue" : "local");
                pos += 2;
            }
        }
        return pos;
    }

    return offset + 1;
}

void slp_disasm_chunk(SlpChunk *chunk, const char *name, FILE *out) {
    fprintf(out, "=== %s ===\n", name);
    int offset = 0;
    while (offset < chunk->count) {
        offset = slp_disasm_instruction(chunk, offset, out);
        fprintf(out, "\n");
    }
}
