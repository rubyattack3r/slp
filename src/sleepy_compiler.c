#include "sleepy_compiler.h"
#include "sleepy_utils.h"
#include <string.h>

static SleepyChunk *current_chunk(SleepyCompiler *compiler) {
    return compiler->function->chunk;
}

static void emit_byte(SleepyCompiler *compiler, uint8_t byte, int line) {
    sleepy_chunk_write(current_chunk(compiler), byte, line);
}

static void emit_short(SleepyCompiler *compiler, uint16_t val, int line) {
    sleepy_chunk_write_short(current_chunk(compiler), val, line);
}

static int emit_jump(SleepyCompiler *compiler, uint8_t opcode, int line) {
    emit_byte(compiler, opcode, line);
    emit_byte(compiler, 0xFF, line);
    emit_byte(compiler, 0xFF, line);
    return current_chunk(compiler)->count - 2;
}

static void patch_jump(SleepyCompiler *compiler, int offset) {
    int jump = current_chunk(compiler)->count - offset - 2;
    current_chunk(compiler)->code[offset] = (uint8_t)((jump >> 8) & 0xFF);
    current_chunk(compiler)->code[offset + 1] = (uint8_t)(jump & 0xFF);
}

static void emit_loop(SleepyCompiler *compiler, int loop_start, int line) {
    emit_byte(compiler, OP_LOOP, line);
    int offset = current_chunk(compiler)->count - loop_start + 2;
    emit_byte(compiler, (uint8_t)((offset >> 8) & 0xFF), line);
    emit_byte(compiler, (uint8_t)(offset & 0xFF), line);
}

static uint16_t make_constant(SleepyCompiler *compiler, SleepyValue value) {
    int idx = sleepy_chunk_add_constant(current_chunk(compiler), value);
    if (idx > UINT16_MAX) return 0;
    return (uint16_t)idx;
}

static void emit_constant(SleepyCompiler *compiler, SleepyValue value, int line) {
    emit_byte(compiler, OP_PUSH_CONST, line);
    emit_short(compiler, make_constant(compiler, value), line);
}

static void emit_return(SleepyCompiler *compiler, int line) {
    emit_byte(compiler, OP_PUSH_NULL, line);
    emit_byte(compiler, OP_RETURN, line);
}

static void __attribute__((unused)) compiler_error(SleepyCompiler *compiler, int line, const char *msg) {
    compiler->had_error = true;
    compiler->error_line = line;
    compiler->error_message = msg;
}

static void init_compiler(SleepyCompiler *compiler, SleepyCompiler *enclosing,
                          SleepyVM *vm, SleepyAllocator *allocator) {
    compiler->enclosing = enclosing;
    compiler->vm = vm;
    compiler->allocator = allocator;
    compiler->function = sleepy_obj_function_new(allocator);
    if (!compiler->function) return;
    compiler->function->obj.next = vm->objects;
    vm->objects = &compiler->function->obj;
    compiler->local_count = 0;
    compiler->scope_depth = 0;
    compiler->upvalue_count = 0;
    compiler->had_error = false;
    compiler->error_line = 0;
    compiler->error_message = NULL;

    SleepyLocal *local = &compiler->locals[compiler->local_count++];
    local->depth = 0;
    local->is_captured = false;
    local->name = NULL;
}

static void begin_scope(SleepyCompiler *compiler) {
    compiler->scope_depth++;
}

static void end_scope(SleepyCompiler *compiler) {
    compiler->scope_depth--;
    while (compiler->local_count > 1 &&
           compiler->locals[compiler->local_count - 1].depth > compiler->scope_depth) {
        if (compiler->locals[compiler->local_count - 1].is_captured)
            emit_byte(compiler, OP_CLOSE_UPVALUE, 0);
        else
            emit_byte(compiler, OP_POP, 0);
        compiler->local_count--;
    }
}

static SleepyObjString *intern_str(SleepyCompiler *compiler, const char *chars, uint32_t len) {
    return sleepy_vm_copy_string(compiler->vm, chars, len);
}

static int resolve_local(SleepyCompiler *compiler, const char *name, uint32_t len) {
    for (int i = compiler->local_count - 1; i >= 0; i--) {
        SleepyLocal *local = &compiler->locals[i];
        if (local->name && local->name->length == len) {
            bool match = true;
            for (uint32_t j = 0; j < len; j++) {
                if (local->name->chars[j] != name[j]) { match = false; break; }
            }
            if (match) return i;
        }
    }
    return -1;
}

static int add_upvalue(SleepyCompiler *compiler, uint8_t index, bool is_local) {
    int upvalue_count = compiler->upvalue_count;
    for (int i = 0; i < upvalue_count; i++) {
        SleepyCompilerUpvalue *uv = &compiler->upvalues[i];
        if (uv->index == index && uv->is_local == is_local)
            return i;
    }
    compiler->upvalues[upvalue_count].index = index;
    compiler->upvalues[upvalue_count].is_local = is_local;
    compiler->upvalue_count++;
    compiler->function->upvalue_count = compiler->upvalue_count;
    return upvalue_count;
}

static int resolve_upvalue(SleepyCompiler *compiler, const char *name, uint32_t len) {
    if (!compiler->enclosing) return -1;
    int local = resolve_local(compiler->enclosing, name, len);
    if (local != -1) {
        compiler->enclosing->locals[local].is_captured = true;
        return add_upvalue(compiler, (uint8_t)local, true);
    }
    int upvalue = resolve_upvalue(compiler->enclosing, name, len);
    if (upvalue != -1)
        return add_upvalue(compiler, (uint8_t)upvalue, false);
    return -1;
}

static void named_variable(SleepyCompiler *compiler, const char *name, uint32_t len, bool assign, int line);
static void compile_node(SleepyCompiler *compiler, SleepyASTNode *node);

static void compile_expr(SleepyCompiler *compiler, SleepyASTNode *node) {
    if (!node) {
        emit_byte(compiler, OP_PUSH_NULL, 0);
        return;
    }
    compile_node(compiler, node);
}

static void compile_node(SleepyCompiler *compiler, SleepyASTNode *node) {
    if (!node) return;
    int line = node->line;

    switch (node->type) {
    case SLEEPY_AST_SCRIPT:
    case SLEEPY_AST_BLOCK: {
        for (size_t i = 0; i < node->as.block.count; i++) {
            compile_node(compiler, node->as.block.statements[i]);
            if (node->as.block.statements[i]->type != SLEEPY_AST_ENV_BRIDGE &&
                node->as.block.statements[i]->type != SLEEPY_AST_IF &&
                node->as.block.statements[i]->type != SLEEPY_AST_WHILE &&
                node->as.block.statements[i]->type != SLEEPY_AST_FOR &&
                node->as.block.statements[i]->type != SLEEPY_AST_FOREACH &&
                node->as.block.statements[i]->type != SLEEPY_AST_ASSERT &&
                node->as.block.statements[i]->type != SLEEPY_AST_TRY_CATCH &&
                node->as.block.statements[i]->type != SLEEPY_AST_RETURN &&
                node->as.block.statements[i]->type != SLEEPY_AST_THROW &&
                node->as.block.statements[i]->type != SLEEPY_AST_YIELD &&
                node->as.block.statements[i]->type != SLEEPY_AST_BREAK &&
                node->as.block.statements[i]->type != SLEEPY_AST_CONTINUE &&
                node->as.block.statements[i]->type != SLEEPY_AST_NOP) {
                emit_byte(compiler, OP_POP, line);
            }
        }
        break;
    }
    case SLEEPY_AST_BOOLEAN:
        emit_byte(compiler, node->as.boolean ? OP_PUSH_TRUE : OP_PUSH_FALSE, line);
        break;
    case SLEEPY_AST_NULL:
        emit_byte(compiler, OP_PUSH_NULL, line);
        break;
    case SLEEPY_AST_NUMBER: {
        SleepyValue val = SLEEPY_NUM_VAL(node->as.double_val);
        emit_constant(compiler, val, line);
        break;
    }
    case SLEEPY_AST_LONG: {
        SleepyObjLong *obj = sleepy_obj_long_new(compiler->allocator, node->as.long_val);
        if (obj) { obj->obj.next = compiler->vm->objects; compiler->vm->objects = &obj->obj; }
        emit_constant(compiler, SLEEPY_OBJ_VAL(obj), line);
        break;
    }
    case SLEEPY_AST_STRING:
    case SLEEPY_AST_LITERAL: {
        if (node->as.string_val) {
            uint32_t slen = (uint32_t)sleepy_utils_strlen(node->as.string_val);
            SleepyObjString *str = intern_str(compiler, node->as.string_val, slen);
            emit_constant(compiler, SLEEPY_OBJ_VAL(str), line);
        } else {
            emit_byte(compiler, OP_PUSH_NULL, line);
        }
        break;
    }
    case SLEEPY_AST_SCALAR: {
        if (node->as.string_val) {
            uint32_t slen = (uint32_t)sleepy_utils_strlen(node->as.string_val);
            named_variable(compiler, node->as.string_val, slen, false, line);
        }
        break;
    }
    case SLEEPY_AST_ARRAY: {
        if (node->as.string_val) {
            uint32_t slen = (uint32_t)sleepy_utils_strlen(node->as.string_val);
            named_variable(compiler, node->as.string_val, slen, false, line);
        } else {
            emit_byte(compiler, OP_PUSH_NULL, line);
        }
        break;
    }
    case SLEEPY_AST_HASHTABLE: {
        if (node->as.string_val) {
            uint32_t slen = (uint32_t)sleepy_utils_strlen(node->as.string_val);
            named_variable(compiler, node->as.string_val, slen, false, line);
        } else {
            emit_byte(compiler, OP_PUSH_NULL, line);
        }
        break;
    }
    case SLEEPY_AST_IDENTIFIER: {
        if (node->as.string_val) {
            uint32_t slen = (uint32_t)sleepy_utils_strlen(node->as.string_val);
            named_variable(compiler, node->as.string_val, slen, false, line);
        }
        break;
    }
    case SLEEPY_AST_BINOP: {
        compile_expr(compiler, node->as.binop.left);
        if (node->as.binop.op.type == SLEEPY_TOKEN_LAND) {
            int end_jump = emit_jump(compiler, OP_AND, line);
            compile_expr(compiler, node->as.binop.right);
            patch_jump(compiler, end_jump);
            break;
        }
        if (node->as.binop.op.type == SLEEPY_TOKEN_LOR) {
            int end_jump = emit_jump(compiler, OP_OR, line);
            compile_expr(compiler, node->as.binop.right);
            patch_jump(compiler, end_jump);
            break;
        }
        compile_expr(compiler, node->as.binop.right);
        SleepyTokenType op_type = node->as.binop.op.type;
        if (node->as.binop.negate)
            emit_byte(compiler, OP_NOT, line);
        switch (op_type) {
        case SLEEPY_TOKEN_PLUS:      emit_byte(compiler, OP_ADD, line); break;
        case SLEEPY_TOKEN_MINUS:     emit_byte(compiler, OP_SUBTRACT, line); break;
        case SLEEPY_TOKEN_STAR:      emit_byte(compiler, OP_MULTIPLY, line); break;
        case SLEEPY_TOKEN_SLASH:     emit_byte(compiler, OP_DIVIDE, line); break;
        case SLEEPY_TOKEN_PERCENT:   emit_byte(compiler, OP_MODULO, line); break;
        case SLEEPY_TOKEN_EXP:       emit_byte(compiler, OP_POWER, line); break;
        case SLEEPY_TOKEN_DOT:       emit_byte(compiler, OP_CONCAT, line); break;
        case SLEEPY_TOKEN_LOWER_X:   emit_byte(compiler, OP_REPEAT, line); break;
        case SLEEPY_TOKEN_EQ:        emit_byte(compiler, OP_EQUAL, line); break;
        case SLEEPY_TOKEN_NE:        emit_byte(compiler, OP_NOT_EQUAL, line); break;
        case SLEEPY_TOKEN_LESS:      emit_byte(compiler, OP_LESS, line); break;
        case SLEEPY_TOKEN_GREATER:   emit_byte(compiler, OP_GREATER, line); break;
        case SLEEPY_TOKEN_LE:        emit_byte(compiler, OP_LESS_EQUAL, line); break;
        case SLEEPY_TOKEN_GE:        emit_byte(compiler, OP_GREATER_EQUAL, line); break;
        case SLEEPY_TOKEN_SPACESHIP: emit_byte(compiler, OP_SPACESHIP, line); break;
        case SLEEPY_TOKEN_EQI:       emit_byte(compiler, OP_MATCH, line); break;
        case SLEEPY_TOKEN_NEQI:      emit_byte(compiler, OP_NOT_MATCH, line); break;
        case SLEEPY_TOKEN_AMPERSAND: emit_byte(compiler, OP_BIT_AND, line); break;
        case SLEEPY_TOKEN_PIPE:      emit_byte(compiler, OP_BIT_OR, line); break;
        case SLEEPY_TOKEN_CARET:     emit_byte(compiler, OP_BIT_XOR, line); break;
        case SLEEPY_TOKEN_LSHIFT:    emit_byte(compiler, OP_LSHIFT, line); break;
        case SLEEPY_TOKEN_RSHIFT:    emit_byte(compiler, OP_RSHIFT, line); break;
        case SLEEPY_TOKEN_BUILTIN_BINARY_PREDICATE_BRIDGE: {
            SleepyObjString *pred_name = intern_str(compiler,
                node->as.binop.op.start, (uint32_t)node->as.binop.op.length);
            emit_byte(compiler, node->as.binop.negate ? OP_NEGATED_BINARY_PREDICATE : OP_BINARY_PREDICATE, line);
            emit_short(compiler, make_constant(compiler, SLEEPY_OBJ_VAL(pred_name)), line);
            break;
        }
        default:
            emit_byte(compiler, OP_ADD, line);
            break;
        }
        break;
    }
    case SLEEPY_AST_UNARYOP: {
        compile_expr(compiler, node->as.unaryop.operand);
        switch (node->as.unaryop.op.type) {
        case SLEEPY_TOKEN_MINUS: emit_byte(compiler, OP_NEGATE, line); break;
        case SLEEPY_TOKEN_BANG:  emit_byte(compiler, OP_NOT, line); break;
        case SLEEPY_TOKEN_INC:   emit_byte(compiler, OP_INCREMENT, line); break;
        case SLEEPY_TOKEN_DEC:   emit_byte(compiler, OP_DECREMENT, line); break;
        default: break;
        }
        break;
    }
    case SLEEPY_AST_ASSIGNMENT: {
        compile_expr(compiler, node->as.assign.right);
        if (node->as.assign.left) {
            switch (node->as.assign.left->type) {
            case SLEEPY_AST_SCALAR:
            case SLEEPY_AST_ARRAY:
            case SLEEPY_AST_HASHTABLE: {
                if (node->as.assign.left->as.string_val) {
                    uint32_t slen = (uint32_t)sleepy_utils_strlen(node->as.assign.left->as.string_val);
                    if (node->as.assign.op.type != SLEEPY_TOKEN_EQUAL) {
                        named_variable(compiler, node->as.assign.left->as.string_val, slen, false, line);
                    }
                    named_variable(compiler, node->as.assign.left->as.string_val, slen, true, line);
                }
                break;
            }
            case SLEEPY_AST_INDEX: {
                compile_expr(compiler, node->as.assign.left->as.index.container);
                compile_expr(compiler, node->as.assign.left->as.index.element);
                emit_byte(compiler, OP_INDEX_SET, line);
                break;
            }
            default:
                break;
            }
        }
        break;
    }
    case SLEEPY_AST_IF: {
        compile_expr(compiler, node->as.if_stmt.condition);
        int then_jump = emit_jump(compiler, OP_JUMP_IF_FALSE, line);
        emit_byte(compiler, OP_POP, line);
        compile_expr(compiler, node->as.if_stmt.then_branch);
        int else_jump = emit_jump(compiler, OP_JUMP, line);
        patch_jump(compiler, then_jump);
        emit_byte(compiler, OP_POP, line);
        if (node->as.if_stmt.else_branch)
            compile_expr(compiler, node->as.if_stmt.else_branch);
        patch_jump(compiler, else_jump);
        break;
    }
    case SLEEPY_AST_WHILE: {
        int outer_loop_start = compiler->loop_start;
        int outer_loop_continue = compiler->loop_continue_target;
        int outer_loop_exit = compiler->loop_exit_jump;
        int outer_loop_depth = compiler->loop_scope_depth;
        int outer_break_count = compiler->break_jump_count;
        int outer_continue_count = compiler->continue_jump_count;
        compiler->loop_start = current_chunk(compiler)->count;
        compiler->loop_continue_target = compiler->loop_start;
        compiler->loop_scope_depth = compiler->scope_depth;
        compiler->break_jump_count = 0;
        compiler->continue_jump_count = 0;
        compile_expr(compiler, node->as.while_stmt.condition);
        int exit_jump = emit_jump(compiler, OP_JUMP_IF_FALSE, line);
        compiler->loop_exit_jump = exit_jump;
        emit_byte(compiler, OP_POP, line);
        compile_expr(compiler, node->as.while_stmt.body);
        emit_loop(compiler, compiler->loop_start, line);
        patch_jump(compiler, exit_jump);
        emit_byte(compiler, OP_POP, line);
        for (int i = 0; i < compiler->break_jump_count; i++)
            patch_jump(compiler, compiler->break_jumps[i]);
        for (int i = 0; i < compiler->continue_jump_count; i++)
            patch_jump(compiler, compiler->continue_jumps[i]);
        compiler->loop_start = outer_loop_start;
        compiler->loop_continue_target = outer_loop_continue;
        compiler->loop_exit_jump = outer_loop_exit;
        compiler->loop_scope_depth = outer_loop_depth;
        compiler->break_jump_count = outer_break_count;
        compiler->continue_jump_count = outer_continue_count;
        break;
    }
    case SLEEPY_AST_FOR: {
        begin_scope(compiler);
        for (size_t i = 0; i < node->as.for_stmt.init_count; i++)
            compile_node(compiler, node->as.for_stmt.initializer[i]);
        int outer_loop_start = compiler->loop_start;
        int outer_loop_continue = compiler->loop_continue_target;
        int outer_loop_exit = compiler->loop_exit_jump;
        int outer_loop_depth = compiler->loop_scope_depth;
        int outer_break_count = compiler->break_jump_count;
        int outer_continue_count = compiler->continue_jump_count;
        compiler->loop_start = current_chunk(compiler)->count;
        compiler->loop_scope_depth = compiler->scope_depth;
        compiler->break_jump_count = 0;
        compiler->continue_jump_count = 0;
        if (node->as.for_stmt.condition) {
            compile_expr(compiler, node->as.for_stmt.condition);
            int exit_jump = emit_jump(compiler, OP_JUMP_IF_FALSE, line);
            compiler->loop_exit_jump = exit_jump;
            emit_byte(compiler, OP_POP, line);
            compile_expr(compiler, node->as.for_stmt.body);
            int inc_start = current_chunk(compiler)->count;
            for (size_t i = 0; i < node->as.for_stmt.inc_count; i++)
                compile_node(compiler, node->as.for_stmt.increment[i]);
            emit_loop(compiler, compiler->loop_start, line);
            patch_jump(compiler, exit_jump);
            emit_byte(compiler, OP_POP, line);
            for (int i = 0; i < compiler->continue_jump_count; i++) {
                current_chunk(compiler)->code[compiler->continue_jumps[i]] =
                    (uint8_t)(((inc_start - compiler->continue_jumps[i] - 2) >> 8) & 0xFF);
                current_chunk(compiler)->code[compiler->continue_jumps[i] + 1] =
                    (uint8_t)((inc_start - compiler->continue_jumps[i] - 2) & 0xFF);
            }
        } else {
            compiler->loop_exit_jump = -1;
            compile_expr(compiler, node->as.for_stmt.body);
            int inc_start = current_chunk(compiler)->count;
            for (size_t i = 0; i < node->as.for_stmt.inc_count; i++)
                compile_node(compiler, node->as.for_stmt.increment[i]);
            emit_loop(compiler, compiler->loop_start, line);
            for (int i = 0; i < compiler->continue_jump_count; i++) {
                current_chunk(compiler)->code[compiler->continue_jumps[i]] =
                    (uint8_t)(((inc_start - compiler->continue_jumps[i] - 2) >> 8) & 0xFF);
                current_chunk(compiler)->code[compiler->continue_jumps[i] + 1] =
                    (uint8_t)((inc_start - compiler->continue_jumps[i] - 2) & 0xFF);
            }
        }
        for (int i = 0; i < compiler->break_jump_count; i++)
            patch_jump(compiler, compiler->break_jumps[i]);
        compiler->loop_start = outer_loop_start;
        compiler->loop_continue_target = outer_loop_continue;
        compiler->loop_exit_jump = outer_loop_exit;
        compiler->loop_scope_depth = outer_loop_depth;
        compiler->break_jump_count = outer_break_count;
        compiler->continue_jump_count = outer_continue_count;
        end_scope(compiler);
        break;
    }
    case SLEEPY_AST_FOREACH: {
        begin_scope(compiler);
        compile_expr(compiler, node->as.foreach.generator);
        emit_byte(compiler, OP_PUSH_CONST, line);
        emit_short(compiler, make_constant(compiler, SLEEPY_NUM_VAL(0)), line);

        int outer_loop_start = compiler->loop_start;
        int outer_loop_continue = compiler->loop_continue_target;
        int outer_loop_exit = compiler->loop_exit_jump;
        int outer_loop_depth = compiler->loop_scope_depth;
        int outer_break_count = compiler->break_jump_count;
        int outer_continue_count = compiler->continue_jump_count;
        compiler->loop_start = current_chunk(compiler)->count;
        compiler->loop_continue_target = compiler->loop_start;
        compiler->loop_scope_depth = compiler->scope_depth;
        compiler->break_jump_count = 0;
        compiler->continue_jump_count = 0;

        int exit_jump = emit_jump(compiler, OP_FOREACH_NEXT, line);
        compiler->loop_exit_jump = exit_jump;

        if (node->as.foreach.index) {
            SleepyObjString *vname = intern_str(compiler, node->as.foreach.value,
                (uint32_t)sleepy_utils_strlen(node->as.foreach.value));
            emit_byte(compiler, OP_STORE_GLOBAL, line);
            emit_short(compiler, make_constant(compiler, SLEEPY_OBJ_VAL(vname)), line);
            emit_byte(compiler, OP_POP, line);

            SleepyObjString *iname = intern_str(compiler, node->as.foreach.index,
                (uint32_t)sleepy_utils_strlen(node->as.foreach.index));
            emit_byte(compiler, OP_STORE_GLOBAL, line);
            emit_short(compiler, make_constant(compiler, SLEEPY_OBJ_VAL(iname)), line);
            emit_byte(compiler, OP_POP, line);
        } else {
            SleepyObjString *vname = intern_str(compiler, node->as.foreach.value,
                (uint32_t)sleepy_utils_strlen(node->as.foreach.value));
            emit_byte(compiler, OP_STORE_GLOBAL, line);
            emit_short(compiler, make_constant(compiler, SLEEPY_OBJ_VAL(vname)), line);
            emit_byte(compiler, OP_POP, line);
            emit_byte(compiler, OP_POP, line); // Pop unused key
        }

        compile_expr(compiler, node->as.foreach.body);

        for (int i = 0; i < compiler->continue_jump_count; i++)
            patch_jump(compiler, compiler->continue_jumps[i]);

        emit_loop(compiler, compiler->loop_start, line);
        patch_jump(compiler, exit_jump);

        for (int i = 0; i < compiler->break_jump_count; i++)
            patch_jump(compiler, compiler->break_jumps[i]);

        emit_byte(compiler, OP_POP, line); // Pop iterator state
        emit_byte(compiler, OP_POP, line); // Pop collection

        compiler->loop_start = outer_loop_start;
        compiler->loop_continue_target = outer_loop_continue;
        compiler->loop_exit_jump = outer_loop_exit;
        compiler->loop_scope_depth = outer_loop_depth;
        compiler->break_jump_count = outer_break_count;
        compiler->continue_jump_count = outer_continue_count;
        end_scope(compiler);
        break;
    }
    case SLEEPY_AST_CALL: {
        compile_expr(compiler, node->as.call.target);
        int pushed_count = 0;
        for (size_t i = 0; i < node->as.call.arg_count; i++) {
            SleepyASTNode *arg = node->as.call.args[i];
            compile_node(compiler, arg);
            if (arg->type == SLEEPY_AST_ARG || arg->type == SLEEPY_AST_KV_PAIR) {
                pushed_count += (arg->as.arg.name ? 2 : 1);
            } else {
                pushed_count += 1;
            }
        }
        emit_byte(compiler, OP_CALL, line);
        emit_byte(compiler, (uint8_t)pushed_count, line);
        break;
    }
    case SLEEPY_AST_RETURN:
        if (node->as.control.value)
            compile_expr(compiler, node->as.control.value);
        else
            emit_byte(compiler, OP_PUSH_NULL, line);
        emit_byte(compiler, OP_RETURN, line);
        break;
    case SLEEPY_AST_THROW:
        compile_expr(compiler, node->as.control.value);
        emit_byte(compiler, OP_THROW, line);
        break;
    case SLEEPY_AST_ASSERT:
        compile_expr(compiler, node->as.control.value);
        emit_byte(compiler, OP_ASSERT, line);
        break;
    case SLEEPY_AST_YIELD:
        if (node->as.control.value)
            compile_expr(compiler, node->as.control.value);
        else
            emit_byte(compiler, OP_PUSH_NULL, line);
        emit_byte(compiler, OP_YIELD, line);
        break;
    case SLEEPY_AST_BREAK:
        if (compiler->loop_start >= 0) {
            int jmp = emit_jump(compiler, OP_JUMP, line);
            if (compiler->break_jump_count < 256)
                compiler->break_jumps[compiler->break_jump_count++] = jmp;
        }
        break;
    case SLEEPY_AST_CONTINUE:
        if (compiler->loop_start >= 0) {
            int jmp = emit_jump(compiler, OP_JUMP, line);
            if (compiler->continue_jump_count < 256)
                compiler->continue_jumps[compiler->continue_jump_count++] = jmp;
        }
        break;
    case SLEEPY_AST_HALT:
        emit_byte(compiler, OP_HALT, line);
        break;
    case SLEEPY_AST_DONE:
        emit_byte(compiler, OP_DONE, line);
        break;
    case SLEEPY_AST_TRY_CATCH: {
        int catch_offset = emit_jump(compiler, OP_PUSH_HANDLER, line);
        compile_expr(compiler, node->as.try_catch.body);
        emit_byte(compiler, OP_POP_HANDLER, line);
        int exit_jump = emit_jump(compiler, OP_JUMP, line);
        patch_jump(compiler, catch_offset);
        if (node->as.try_catch.value) {
            SleepyObjString *vname = intern_str(compiler,
                node->as.try_catch.value,
                (uint32_t)sleepy_utils_strlen(node->as.try_catch.value));
            emit_byte(compiler, OP_STORE_GLOBAL, line);
            emit_short(compiler, make_constant(compiler, SLEEPY_OBJ_VAL(vname)), line);
        }
        compile_expr(compiler, node->as.try_catch.handler);
        patch_jump(compiler, exit_jump);
        break;
    }
    case SLEEPY_AST_ENV_BRIDGE: {
        SleepyCompiler sub_compiler;
        init_compiler(&sub_compiler, compiler, compiler->vm, compiler->allocator);
        if (node->as.env_bridge.body)
            compile_node(&sub_compiler, node->as.env_bridge.body);
        emit_return(&sub_compiler, line);
        if (node->as.env_bridge.keyword) {
            SleepyObjString *kw = intern_str(&sub_compiler, node->as.env_bridge.keyword,
                (uint32_t)sleepy_utils_strlen(node->as.env_bridge.keyword));
            sub_compiler.function->name = kw;
        }
        SleepyObjFunction *fn = sub_compiler.function;
        emit_byte(compiler, OP_CLOSURE, line);
        emit_short(compiler, make_constant(compiler, SLEEPY_OBJ_VAL(fn)), line);
        for (int i = 0; i < fn->upvalue_count; i++) {
            emit_byte(compiler, sub_compiler.upvalues[i].is_local ? 1 : 0, line);
            emit_byte(compiler, sub_compiler.upvalues[i].index, line);
        }
        if (node->as.env_bridge.keyword) {
            SleepyObjString *kw = intern_str(compiler, node->as.env_bridge.keyword,
                (uint32_t)sleepy_utils_strlen(node->as.env_bridge.keyword));
            emit_byte(compiler, OP_BRIDGE_REGISTER, line);
            emit_short(compiler, make_constant(compiler, SLEEPY_OBJ_VAL(kw)), line);
            if (node->as.env_bridge.identifier) {
                SleepyObjString *name = intern_str(compiler, node->as.env_bridge.identifier,
                    (uint32_t)sleepy_utils_strlen(node->as.env_bridge.identifier));
                emit_short(compiler, make_constant(compiler, SLEEPY_OBJ_VAL(name)), line);
            } else {
                emit_short(compiler, make_constant(compiler, SLEEPY_OBJ_VAL(
                    intern_str(compiler, "", 0))), line);
            }
        }
        break;
    }
    case SLEEPY_AST_IMPORT: {
        if (node->as.import_stmt.target) {
            SleepyObjString *target = intern_str(compiler, node->as.import_stmt.target,
                (uint32_t)sleepy_utils_strlen(node->as.import_stmt.target));
            emit_byte(compiler, OP_IMPORT, line);
            emit_short(compiler, make_constant(compiler, SLEEPY_OBJ_VAL(target)), line);
            if (node->as.import_stmt.path) {
                SleepyObjString *path = intern_str(compiler, node->as.import_stmt.path,
                    (uint32_t)sleepy_utils_strlen(node->as.import_stmt.path));
                emit_short(compiler, make_constant(compiler, SLEEPY_OBJ_VAL(path)), line);
            } else {
                emit_short(compiler, 0, line);
            }
        }
        break;
    }
    case SLEEPY_AST_INDEX:
        compile_expr(compiler, node->as.index.container);
        compile_expr(compiler, node->as.index.element);
        emit_byte(compiler, OP_INDEX_GET, line);
        break;
    case SLEEPY_AST_OBJ_EXPR:
        compile_expr(compiler, node->as.obj_expr.target);
        if (node->as.obj_expr.message)
            compile_expr(compiler, node->as.obj_expr.message);
        emit_byte(compiler, OP_OBJ_EXPR, line);
        emit_byte(compiler, (uint8_t)node->as.obj_expr.arg_count, line);
        break;
    case SLEEPY_AST_BACKTICK: {
        if (node->as.string_val) {
            SleepyObjString *cmd = intern_str(compiler, node->as.string_val,
                (uint32_t)sleepy_utils_strlen(node->as.string_val));
            emit_byte(compiler, OP_BACKTICK, line);
            emit_short(compiler, make_constant(compiler, SLEEPY_OBJ_VAL(cmd)), line);
        }
        break;
    }
    case SLEEPY_AST_CLASS_LITERAL: {
        if (node->as.string_val) {
            SleepyObjString *cls = intern_str(compiler, node->as.string_val,
                (uint32_t)sleepy_utils_strlen(node->as.string_val));
            emit_byte(compiler, OP_CLASS_LITERAL, line);
            emit_short(compiler, make_constant(compiler, SLEEPY_OBJ_VAL(cls)), line);
        }
        break;
    }
    case SLEEPY_AST_ADDRESS: {
        if (node->as.string_val) {
            SleepyObjString *addr = intern_str(compiler, node->as.string_val,
                (uint32_t)sleepy_utils_strlen(node->as.string_val));
            emit_byte(compiler, OP_ADDRESS, line);
            emit_short(compiler, make_constant(compiler, SLEEPY_OBJ_VAL(addr)), line);
        }
        break;
    }
    case SLEEPY_AST_LOCAL:
        emit_byte(compiler, OP_LOCAL, line);
        emit_byte(compiler, 1, line);
        break;
    case SLEEPY_AST_THIS:
        emit_byte(compiler, OP_THIS, line);
        break;
    case SLEEPY_AST_CALLCC:
        if (node->as.control.value)
            compile_expr(compiler, node->as.control.value);
        emit_byte(compiler, OP_CALLCC, line);
        emit_byte(compiler, node->as.control.value ? 1 : 0, line);
        break;
    case SLEEPY_AST_LVALUE_TUPLE:
        for (size_t i = 0; i < node->as.block.count; i++)
            compile_node(compiler, node->as.block.statements[i]);
        emit_byte(compiler, OP_UNPACK_TUPLE, line);
        emit_byte(compiler, (uint8_t)node->as.block.count, line);
        break;
    case SLEEPY_AST_ASSIGN_LOOP:
        compile_expr(compiler, node->as.assign_loop.generator);
        compile_expr(compiler, node->as.assign_loop.body);
        break;
    case SLEEPY_AST_ARG:
    case SLEEPY_AST_KV_PAIR:
        if (node->as.arg.name)
            compile_expr(compiler, node->as.arg.name);
        compile_expr(compiler, node->as.arg.value);
        break;
    case SLEEPY_AST_NOP:
        emit_byte(compiler, OP_NOP, line);
        break;
    default:
        break;
    }
}

static void named_variable(SleepyCompiler *compiler, const char *name, uint32_t len, bool assign, int line) {
    int arg = resolve_local(compiler, name, len);
    SleepyOpcode get_op, set_op;
    if (arg != -1) {
        if (arg < 8) {
            get_op = (SleepyOpcode)(OP_LOAD_LOCAL_0 + arg);
            set_op = (SleepyOpcode)(OP_STORE_LOCAL_0 + arg);
        } else {
            get_op = OP_LOAD_LOCAL;
            set_op = OP_STORE_LOCAL;
        }
    } else if ((arg = resolve_upvalue(compiler, name, len)) != -1) {
        get_op = OP_LOAD_UPVALUE;
        set_op = OP_STORE_UPVALUE;
    } else {
        get_op = OP_LOAD_GLOBAL;
        set_op = OP_STORE_GLOBAL;
        SleepyObjString *str = intern_str(compiler, name, len);
        arg = sleepy_chunk_add_constant(current_chunk(compiler), SLEEPY_OBJ_VAL(str));
    }

    if (assign) {
        if (set_op == OP_LOAD_LOCAL || set_op == OP_STORE_LOCAL) {
            emit_byte(compiler, OP_STORE_LOCAL, line);
            emit_byte(compiler, (uint8_t)arg, line);
        } else if (set_op == OP_STORE_UPVALUE) {
            emit_byte(compiler, OP_STORE_UPVALUE, line);
            emit_byte(compiler, (uint8_t)arg, line);
        } else {
            emit_byte(compiler, OP_STORE_GLOBAL, line);
            emit_short(compiler, (uint16_t)arg, line);
        }
    } else {
        if (get_op >= OP_LOAD_LOCAL_0 && get_op <= OP_LOAD_LOCAL_7) {
            emit_byte(compiler, get_op, line);
        } else if (get_op == OP_LOAD_LOCAL) {
            emit_byte(compiler, OP_LOAD_LOCAL, line);
            emit_byte(compiler, (uint8_t)arg, line);
        } else if (get_op == OP_LOAD_UPVALUE) {
            emit_byte(compiler, OP_LOAD_UPVALUE, line);
            emit_byte(compiler, (uint8_t)arg, line);
        } else {
            emit_byte(compiler, OP_LOAD_GLOBAL, line);
            emit_short(compiler, (uint16_t)arg, line);
        }
    }
}

SleepyObjFunction *sleepy_compile(SleepyVM *vm, SleepyASTNode *ast, SleepyAllocator *allocator) {
    SleepyCompiler compiler;
    init_compiler(&compiler, NULL, vm, allocator);
    if (!compiler.function) return NULL;
    compile_node(&compiler, ast);
    emit_return(&compiler, ast ? ast->line : 0);
    if (compiler.had_error) return NULL;
    return compiler.function;
}
