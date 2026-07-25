#include "slp_compiler.h"
#include "slp_utils.h"
#include <string.h>
#include <stdio.h>

static SlpChunk *current_chunk(SlpCompiler *compiler) {
    return compiler->function->chunk;
}

static void emit_byte(SlpCompiler *compiler, uint8_t byte, int line) {
    slp_chunk_write(current_chunk(compiler), byte, line);
}

static void emit_short(SlpCompiler *compiler, uint16_t val, int line) {
    slp_chunk_write_short(current_chunk(compiler), val, line);
}

static int emit_jump(SlpCompiler *compiler, uint8_t opcode, int line) {
    emit_byte(compiler, opcode, line);
    emit_byte(compiler, 0xFF, line);
    emit_byte(compiler, 0xFF, line);
    return current_chunk(compiler)->count - 2;
}

static void compiler_error(SlpCompiler *compiler, int line, const char *msg);

static void patch_jump(SlpCompiler *compiler, int offset) {
    int jump = current_chunk(compiler)->count - offset - 2;
    // Forward jumps encode an unsigned 16-bit operand (see OP_JUMP in the VM).
    if (jump > UINT16_MAX) {
        compiler_error(compiler, 0, "Too much code to jump over.");
        return;
    }
    current_chunk(compiler)->code[offset] = (uint8_t)((jump >> 8) & 0xFF);
    current_chunk(compiler)->code[offset + 1] = (uint8_t)(jump & 0xFF);
}

static void emit_loop(SlpCompiler *compiler, int loop_start, int line) {
    emit_byte(compiler, OP_LOOP, line);
    int offset = current_chunk(compiler)->count - loop_start + 2;
    // OP_LOOP decodes its operand as a signed int16_t, so the back-jump
    // distance must fit in INT16_MAX.
    if (offset > INT16_MAX) {
        compiler_error(compiler, line, "Loop body too large.");
        return;
    }
    emit_byte(compiler, (uint8_t)((offset >> 8) & 0xFF), line);
    emit_byte(compiler, (uint8_t)(offset & 0xFF), line);
}

static uint16_t make_constant(SlpCompiler *compiler, SlpValue value) {
    int idx = slp_chunk_add_constant(current_chunk(compiler), value);
    if (idx < 0 || idx > UINT16_MAX) {
        compiler_error(compiler, 0, "Too many constants in one chunk.");
        return 0;
    }
    return (uint16_t)idx;
}

static void emit_constant(SlpCompiler *compiler, SlpValue value, int line) {
    emit_byte(compiler, OP_PUSH_CONST, line);
    emit_short(compiler, make_constant(compiler, value), line);
}

static void emit_return(SlpCompiler *compiler, int line) {
    emit_byte(compiler, OP_PUSH_NULL, line);
    emit_byte(compiler, OP_RETURN, line);
}

static void compiler_error(SlpCompiler *compiler, int line, const char *msg) {
    if (compiler->had_error) return;
    compiler->had_error = true;
    compiler->error_line = line;
    compiler->error_message = msg;
}

static SlpObjString *intern_str(SlpCompiler *compiler, const char *chars, uint32_t len);

static void init_compiler(SlpCompiler *compiler, SlpCompiler *enclosing,
                          SlpVM *vm, SlpAllocator *allocator) {
    compiler->enclosing = enclosing;
    compiler->vm = vm;
    compiler->allocator = allocator;
    compiler->function = slp_obj_function_new(allocator);
    if (!compiler->function) return;
    compiler->function->obj.next = vm->objects;
    vm->objects = &compiler->function->obj;
    if (vm->source_name) {
        compiler->function->source_name = slp_vm_intern_string(
            vm, vm->source_name, (uint32_t)strlen(vm->source_name));
    }
    compiler->local_count = 0;
    compiler->scope_depth = 0;
    compiler->upvalue_count = 0;
    compiler->loop_start = -1;
    compiler->loop_continue_target = -1;
    compiler->loop_exit_jump = -1;
    compiler->loop_scope_depth = 0;
    compiler->try_depth = 0;
    compiler->loop_try_depth = 0;
    compiler->break_jump_count = 0;
    compiler->continue_jump_count = 0;
    compiler->had_error = false;
    compiler->error_line = 0;
    compiler->error_message = NULL;
    compiler->repl_mode = enclosing ? enclosing->repl_mode : false;
    compiler->inline_body = false;

    SlpLocal *local = &compiler->locals[compiler->local_count++];
    local->depth = 0;
    local->is_captured = false;
    local->name = NULL;

    if (enclosing != NULL) {
        for (int i = 0; i <= 9; i++) {
            char name[3] = {'$', (char)('0' + i), '\0'};
            SlpLocal *arg_local = &compiler->locals[compiler->local_count++];
            arg_local->depth = 0;
            arg_local->is_captured = false;
            arg_local->name = intern_str(compiler, name, 2);
        }
        SlpLocal *arg_local = &compiler->locals[compiler->local_count++];
        arg_local->depth = 0;
        arg_local->is_captured = false;
        arg_local->name = intern_str(compiler, "@_", 2);
    }
}

static void begin_scope(SlpCompiler *compiler) {
    compiler->scope_depth++;
}

static void end_scope(SlpCompiler *compiler) {
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

static SlpObjString *intern_str(SlpCompiler *compiler, const char *chars, uint32_t len) {
    return slp_vm_copy_string(compiler->vm, chars, len);
}

static int resolve_local(SlpCompiler *compiler, const char *name, uint32_t len) {
    for (int i = compiler->local_count - 1; i >= 0; i--) {
        SlpLocal *local = &compiler->locals[i];
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

static int add_upvalue(SlpCompiler *compiler, uint8_t index, bool is_local) {
    int upvalue_count = compiler->upvalue_count;
    for (int i = 0; i < upvalue_count; i++) {
        SlpCompilerUpvalue *uv = &compiler->upvalues[i];
        if (uv->index == index && uv->is_local == is_local)
            return i;
    }
    // The upvalues array holds 256 slots and the closure operand is one byte.
    if (upvalue_count >= 256) {
        compiler_error(compiler, 0, "Too many closure variables in function.");
        return 0;
    }
    compiler->upvalues[upvalue_count].index = index;
    compiler->upvalues[upvalue_count].is_local = is_local;
    compiler->upvalue_count++;
    compiler->function->upvalue_count = compiler->upvalue_count;
    return upvalue_count;
}

static int resolve_upvalue(SlpCompiler *compiler, const char *name, uint32_t len) {
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

static void named_variable(SlpCompiler *compiler, const char *name, uint32_t len, bool assign, int line);
static void reference_variable(SlpCompiler *compiler, const char *name,
                               uint32_t len, int line);
static void compile_node(SlpCompiler *compiler, SlpASTNode *node);

static bool variable_node_name(SlpASTNode *node, char *buffer,
                               size_t buffer_size) {
    if (!node || !node->as.string_val) return false;
    switch (node->type) {
    case SLP_AST_SCALAR:
        snprintf(buffer, buffer_size, "$%s", node->as.string_val);
        return true;
    case SLP_AST_ARRAY:
        if (strcmp(node->as.string_val, "@") == 0)
            snprintf(buffer, buffer_size, "%s", node->as.string_val);
        else
            snprintf(buffer, buffer_size, "@%s", node->as.string_val);
        return true;
    case SLP_AST_HASHTABLE:
        if (strcmp(node->as.string_val, "%") == 0)
            snprintf(buffer, buffer_size, "%s", node->as.string_val);
        else
            snprintf(buffer, buffer_size, "%%%s", node->as.string_val);
        return true;
    default:
        return false;
    }
}

static SlpOpcode assignment_operation_opcode(SlpTokenType type) {
    switch (type) {
    case SLP_TOKEN_PLUSEQUAL: return OP_ADD;
    case SLP_TOKEN_MINUSEQUAL: return OP_SUBTRACT;
    case SLP_TOKEN_TIMESEQUAL: return OP_MULTIPLY;
    case SLP_TOKEN_DIVEQUAL: return OP_DIVIDE;
    case SLP_TOKEN_CATEQUAL: return OP_CONCAT;
    case SLP_TOKEN_ANDEQUAL: return OP_BIT_AND;
    case SLP_TOKEN_OREQUAL: return OP_BIT_OR;
    case SLP_TOKEN_XOREQUAL: return OP_BIT_XOR;
    case SLP_TOKEN_LSHIFTEQUAL: return OP_LSHIFT;
    case SLP_TOKEN_RSHIFTEQUAL: return OP_RSHIFT;
    case SLP_TOKEN_EXPEQUAL: return OP_POWER;
    default: return OP_NOP;
    }
}

static void compile_expr(SlpCompiler *compiler, SlpASTNode *node) {
    if (!node) {
        emit_byte(compiler, OP_PUSH_NULL, 0);
        return;
    }
    compile_node(compiler, node);
}

static uint8_t index_autovivify_kind(SlpASTNode *index) {
    SlpASTNode *root = index;
    while (root && root->type == SLP_AST_INDEX)
        root = root->as.index.container;
    if (root && root->type == SLP_AST_ARRAY) return 1;
    if (root && root->type == SLP_AST_HASHTABLE) return 2;
    return 0;
}

static bool assert_condition_is_predicate(SlpASTNode *condition) {
    if (!condition || condition->type != SLP_AST_BINOP)
        return true;

    switch (condition->as.binop.op.type) {
    case SLP_TOKEN_LAND:
    case SLP_TOKEN_LOR:
    case SLP_TOKEN_EQ:
    case SLP_TOKEN_NE:
    case SLP_TOKEN_LESS:
    case SLP_TOKEN_GREATER:
    case SLP_TOKEN_LE:
    case SLP_TOKEN_GE:
    case SLP_TOKEN_EQI:
    case SLP_TOKEN_NEQI:
    case SLP_TOKEN_BUILTIN_BINARY_PREDICATE_BRIDGE:
        return true;
    default:
        /*
         * Sleep parses an assert operand as a predicate, not as a general
         * value expression. An ungrouped arithmetic operator therefore does
         * not form a true assertion even when its numeric result is nonzero.
         */
        return false;
    }
}

static bool condition_is_explicit_predicate(
    SlpASTNode *condition) {
    if (!condition) return false;
    if (condition->type == SLP_AST_UNARYOP)
        return condition->as.unaryop.op.type ==
               SLP_TOKEN_UNARY_PREDICATE_BRIDGE;
    if (condition->type != SLP_AST_BINOP)
        return false;
    switch (condition->as.binop.op.type) {
    case SLP_TOKEN_LAND:
    case SLP_TOKEN_LOR:
    case SLP_TOKEN_EQ:
    case SLP_TOKEN_NE:
    case SLP_TOKEN_LESS:
    case SLP_TOKEN_GREATER:
    case SLP_TOKEN_LE:
    case SLP_TOKEN_GE:
    case SLP_TOKEN_EQI:
    case SLP_TOKEN_NEQI:
    case SLP_TOKEN_BUILTIN_BINARY_PREDICATE_BRIDGE:
        return true;
    default:
        return false;
    }
}

static void emit_truth_predicate(
    SlpCompiler *compiler, int line) {
    SlpObjString *predicate =
        intern_str(compiler, "-istrue", 7);
    emit_byte(compiler, OP_UNARY_PREDICATE, line);
    emit_short(
        compiler,
        make_constant(compiler, SLP_OBJ_VAL(predicate)), line);
}

static SlpASTNode *call_argument_value(SlpASTNode *argument) {
    if (argument &&
        (argument->type == SLP_AST_ARG ||
         argument->type == SLP_AST_KV_PAIR))
        return argument->as.arg.value;
    return argument;
}

static bool is_iff_call(SlpASTNode *node) {
    return node && node->type == SLP_AST_CALL &&
           node->as.call.target &&
           node->as.call.target->type == SLP_AST_IDENTIFIER &&
           node->as.call.target->as.string_val &&
           strcmp(node->as.call.target->as.string_val, "iff") == 0;
}

static void compile_iff_call(SlpCompiler *compiler, SlpASTNode *node,
                             int line) {
    if (node->as.call.arg_count == 0) {
        emit_byte(compiler, OP_PUSH_NULL, line);
        return;
    }

    SlpASTNode *condition =
        call_argument_value(node->as.call.args[0]);
    if (assert_condition_is_predicate(condition))
        compile_expr(compiler, condition);
    else
        emit_byte(compiler, OP_PUSH_FALSE, line);

    int false_jump = emit_jump(compiler, OP_JUMP_IF_FALSE, line);
    emit_byte(compiler, OP_POP, line);
    if (node->as.call.arg_count >= 2)
        compile_expr(
            compiler, call_argument_value(node->as.call.args[1]));
    else
        emit_constant(compiler, SLP_NUM_VAL(1.0), line);
    int end_jump = emit_jump(compiler, OP_JUMP, line);

    patch_jump(compiler, false_jump);
    emit_byte(compiler, OP_POP, line);
    if (node->as.call.arg_count >= 3)
        compile_expr(
            compiler, call_argument_value(node->as.call.args[2]));
    else
        emit_byte(compiler, OP_PUSH_NULL, line);
    patch_jump(compiler, end_jump);
}

static void compile_assignment_container(
    SlpCompiler *compiler, SlpASTNode *container, uint8_t kind, int line) {
    if (container && container->type == SLP_AST_INDEX) {
        compile_assignment_container(
            compiler, container->as.index.container, kind, line);
        compile_expr(compiler, container->as.index.element);
        emit_byte(compiler, OP_INDEX_ENSURE, line);
        emit_byte(compiler, kind, line);
        return;
    }
    compile_expr(compiler, container);
}

static void compile_node(SlpCompiler *compiler, SlpASTNode *node) {
    if (!node) return;
    int line = node->line;

    switch (node->type) {
    case SLP_AST_SCRIPT:
    case SLP_AST_BLOCK: {
        for (size_t i = 0; i < node->as.block.count; i++) {
            bool auto_print = false;
            SlpASTNodeType stype = node->as.block.statements[i]->type;

            if (compiler->repl_mode && node->type == SLP_AST_SCRIPT && i == node->as.block.count - 1) {
                if (stype != SLP_AST_ENV_BRIDGE && stype != SLP_AST_IF &&
                    stype != SLP_AST_WHILE && stype != SLP_AST_FOR &&
                    stype != SLP_AST_FOREACH && stype != SLP_AST_ASSERT &&
                    stype != SLP_AST_TRY_CATCH && stype != SLP_AST_RETURN &&
                    stype != SLP_AST_THROW && stype != SLP_AST_YIELD &&
                    stype != SLP_AST_BREAK && stype != SLP_AST_CONTINUE &&
                    stype != SLP_AST_NOP) {
                    
                    auto_print = true;
                }
            }

            compile_node(compiler, node->as.block.statements[i]);

            if (stype != SLP_AST_ENV_BRIDGE && stype != SLP_AST_IF &&
                stype != SLP_AST_WHILE && stype != SLP_AST_FOR &&
                stype != SLP_AST_FOREACH && stype != SLP_AST_ASSERT &&
                stype != SLP_AST_TRY_CATCH && stype != SLP_AST_RETURN &&
                stype != SLP_AST_THROW && stype != SLP_AST_YIELD &&
                stype != SLP_AST_BREAK && stype != SLP_AST_CONTINUE &&
                stype != SLP_AST_NOP) {
                int statement_line = node->as.block.statements[i]->line;
                if (auto_print) {
                    emit_byte(compiler, OP_RETURN, statement_line);
                } else {
                    emit_byte(compiler, OP_POP, statement_line);
                }
            }
        }
        break;
    }
    case SLP_AST_BOOLEAN:
        emit_byte(compiler, node->as.boolean ? OP_PUSH_TRUE : OP_PUSH_FALSE, line);
        break;
    case SLP_AST_NULL:
        emit_byte(compiler, OP_PUSH_NULL, line);
        break;
    case SLP_AST_NUMBER: {
        if (node->number_is_double) {
            SlpObjDouble *object =
                slp_vm_new_double(compiler->vm, node->as.double_val);
            emit_constant(compiler,
                          object ? SLP_OBJ_VAL(object) : SLP_NULL_VAL,
                          line);
        } else {
            emit_constant(compiler, SLP_NUM_VAL(node->as.double_val), line);
        }
        break;
    }
    case SLP_AST_LONG: {
        SlpObjLong *obj =
            slp_vm_new_long(compiler->vm, node->as.long_val);
        emit_constant(compiler,
                      obj ? SLP_OBJ_VAL(obj) : SLP_NULL_VAL, line);
        break;
    }
    case SLP_AST_STRING:
    case SLP_AST_LITERAL: {
        if (node->as.string_val) {
            uint32_t slen = (uint32_t)slp_utils_strlen(node->as.string_val);
            SlpObjString *str = intern_str(compiler, node->as.string_val, slen);
            emit_constant(compiler, SLP_OBJ_VAL(str), line);
        } else {
            emit_byte(compiler, OP_PUSH_NULL, line);
        }
        break;
    }
    case SLP_AST_ALIGN:
        compile_expr(compiler, node->as.align.width);
        compile_expr(compiler, node->as.align.value);
        emit_byte(compiler, OP_ALIGN, line);
        break;
    case SLP_AST_SCALAR: {
        if (node->as.string_val) {
            char name_buf[256];
            snprintf(name_buf, sizeof(name_buf), "$%s", node->as.string_val);
            named_variable(compiler, name_buf, (uint32_t)strlen(name_buf), false, line);
        }
        break;
    }
    case SLP_AST_ARRAY: {
        if (node->as.string_val) {
            char name_buf[256];
            if (strcmp(node->as.string_val, "@") == 0) {
                snprintf(name_buf, sizeof(name_buf), "%s", node->as.string_val);
            } else {
                snprintf(name_buf, sizeof(name_buf), "@%s", node->as.string_val);
            }
            named_variable(compiler, name_buf, (uint32_t)strlen(name_buf), false, line);
        } else {
            emit_byte(compiler, OP_PUSH_NULL, line);
        }
        break;
    }
    case SLP_AST_HASHTABLE: {
        if (node->as.string_val) {
            char name_buf[256];
            if (strcmp(node->as.string_val, "%") == 0) {
                snprintf(name_buf, sizeof(name_buf), "%s", node->as.string_val);
            } else {
                snprintf(name_buf, sizeof(name_buf), "%%%s", node->as.string_val);
            }
            named_variable(compiler, name_buf, (uint32_t)strlen(name_buf), false, line);
        } else {
            emit_byte(compiler, OP_PUSH_NULL, line);
        }
        break;
    }
    case SLP_AST_IDENTIFIER: {
        compiler_error(
            compiler, line,
            "Unknown expression");
        emit_byte(
            compiler, OP_PUSH_NULL, line);
        break;
    }
    case SLP_AST_BINOP: {
        if (node->as.binop.op.type == SLP_TOKEN_LAND) {
            compile_expr(compiler, node->as.binop.left);
            int end_jump = emit_jump(compiler, OP_AND, line);
            compile_expr(compiler, node->as.binop.right);
            patch_jump(compiler, end_jump);
            break;
        }
        if (node->as.binop.op.type == SLP_TOKEN_LOR) {
            compile_expr(compiler, node->as.binop.left);
            int end_jump = emit_jump(compiler, OP_OR, line);
            compile_expr(compiler, node->as.binop.right);
            patch_jump(compiler, end_jump);
            break;
        }
        if (node->as.binop.op.type ==
            SLP_TOKEN_BUILTIN_BINARY_PREDICATE_BRIDGE) {
            /*
             * Sleep's predicate bridges are the exception to its usual
             * right-to-left operand evaluation: predicates evaluate their
             * left operand first.
             */
            compile_expr(compiler, node->as.binop.left);
            compile_expr(compiler, node->as.binop.right);
            SlpObjString *pred_name = intern_str(
                compiler, node->as.binop.op.start,
                (uint32_t)node->as.binop.op.length);
            emit_byte(
                compiler,
                node->as.binop.negate
                    ? OP_NEGATED_BINARY_PREDICATE
                    : OP_BINARY_PREDICATE,
                line);
            emit_short(
                compiler,
                make_constant(compiler, SLP_OBJ_VAL(pred_name)), line);
            break;
        }
        /* Sleep evaluates ordinary binary operands right-to-left. Keep that
           observable side-effect order, then restore the stack layout used by
           the existing binary opcodes. */
        compile_expr(compiler, node->as.binop.right);
        compile_expr(compiler, node->as.binop.left);
        emit_byte(compiler, OP_SWAP, line);
        SlpTokenType op_type = node->as.binop.op.type;
        switch (op_type) {
        case SLP_TOKEN_PLUS:      emit_byte(compiler, OP_ADD, line); break;
        case SLP_TOKEN_MINUS:     emit_byte(compiler, OP_SUBTRACT, line); break;
        case SLP_TOKEN_STAR:      emit_byte(compiler, OP_MULTIPLY, line); break;
        case SLP_TOKEN_SLASH:     emit_byte(compiler, OP_DIVIDE, line); break;
        case SLP_TOKEN_PERCENT:   emit_byte(compiler, OP_MODULO, line); break;
        case SLP_TOKEN_EXP:       emit_byte(compiler, OP_POWER, line); break;
        case SLP_TOKEN_DOT:       emit_byte(compiler, OP_CONCAT, line); break;
        case SLP_TOKEN_LOWER_X:   emit_byte(compiler, OP_REPEAT, line); break;
        case SLP_TOKEN_EQ:        emit_byte(compiler, OP_EQUAL, line); break;
        case SLP_TOKEN_NE:        emit_byte(compiler, OP_NOT_EQUAL, line); break;
        case SLP_TOKEN_LESS:      emit_byte(compiler, OP_LESS, line); break;
        case SLP_TOKEN_GREATER:   emit_byte(compiler, OP_GREATER, line); break;
        case SLP_TOKEN_LE:        emit_byte(compiler, OP_LESS_EQUAL, line); break;
        case SLP_TOKEN_GE:        emit_byte(compiler, OP_GREATER_EQUAL, line); break;
        case SLP_TOKEN_SPACESHIP: emit_byte(compiler, OP_SPACESHIP, line); break;
        case SLP_TOKEN_EQI:       emit_byte(compiler, OP_MATCH, line); break;
        case SLP_TOKEN_NEQI:      emit_byte(compiler, OP_NOT_MATCH, line); break;
        case SLP_TOKEN_AMPERSAND: emit_byte(compiler, OP_BIT_AND, line); break;
        case SLP_TOKEN_PIPE:      emit_byte(compiler, OP_BIT_OR, line); break;
        case SLP_TOKEN_CARET:     emit_byte(compiler, OP_BIT_XOR, line); break;
        case SLP_TOKEN_LSHIFT:    emit_byte(compiler, OP_LSHIFT, line); break;
        case SLP_TOKEN_RSHIFT:    emit_byte(compiler, OP_RSHIFT, line); break;
        default:
            emit_byte(compiler, OP_ADD, line);
            break;
        }
        break;
    }
    case SLP_AST_UNARYOP: {
        SlpToken op = node->as.unaryop.op;
        SlpASTNode *operand = node->as.unaryop.operand;
        if (op.type == SLP_TOKEN_INC || op.type == SLP_TOKEN_DEC) {
            if (operand && operand->as.string_val) {
                char name_buf[256];
                if (operand->type == SLP_AST_SCALAR) {
                    snprintf(name_buf, sizeof(name_buf), "$%s", operand->as.string_val);
                } else if (operand->type == SLP_AST_ARRAY) {
                    snprintf(name_buf, sizeof(name_buf), "@%s", operand->as.string_val);
                } else if (operand->type == SLP_AST_HASHTABLE) {
                    snprintf(name_buf, sizeof(name_buf), "%%%s", operand->as.string_val);
                } else {
                    snprintf(name_buf, sizeof(name_buf), "%s", operand->as.string_val);
                }
                uint32_t slen = (uint32_t)strlen(name_buf);
                named_variable(compiler, name_buf, slen, false, line);
                if (node->as.unaryop.is_postfix) {
                    emit_byte(compiler, op.type == SLP_TOKEN_INC ? OP_INCREMENT : OP_DECREMENT, line);
                    named_variable(compiler, name_buf, slen, true, line);
                } else {
                    emit_byte(compiler, op.type == SLP_TOKEN_INC ? OP_INCREMENT : OP_DECREMENT, line);
                    named_variable(compiler, name_buf, slen, true, line);
                }
            } else {
                compile_expr(compiler, operand);
                emit_byte(compiler, op.type == SLP_TOKEN_INC ? OP_INCREMENT : OP_DECREMENT, line);
            }
        } else {
            compile_expr(compiler, operand);
            switch (op.type) {
            case SLP_TOKEN_MINUS: emit_byte(compiler, OP_NEGATE, line); break;
            case SLP_TOKEN_BANG:  emit_byte(compiler, OP_NOT, line); break;
            case SLP_TOKEN_UNARY_PREDICATE_BRIDGE: {
                SlpObjString *pred_name = intern_str(compiler,
                    op.start, (uint32_t)op.length);
                emit_byte(compiler, OP_UNARY_PREDICATE, line);
                emit_short(compiler, make_constant(compiler, SLP_OBJ_VAL(pred_name)), line);
                break;
            }
            default: break;
            }
        }
        break;
    }
    case SLP_AST_ASSIGNMENT: {
        if (node->as.assign.left) {
            switch (node->as.assign.left->type) {
            case SLP_AST_SCALAR:
            case SLP_AST_ARRAY:
            case SLP_AST_HASHTABLE:
            case SLP_AST_IDENTIFIER: {
                if (node->as.assign.left->as.string_val) {
                    char name_buf[256];
                    if (node->as.assign.left->type == SLP_AST_SCALAR) {
                        snprintf(name_buf, sizeof(name_buf), "$%s", node->as.assign.left->as.string_val);
                    } else if (node->as.assign.left->type == SLP_AST_ARRAY) {
                        if (strcmp(node->as.assign.left->as.string_val, "@") == 0) {
                            snprintf(name_buf, sizeof(name_buf), "%s", node->as.assign.left->as.string_val);
                        } else {
                            snprintf(name_buf, sizeof(name_buf), "@%s", node->as.assign.left->as.string_val);
                        }
                    } else if (node->as.assign.left->type == SLP_AST_HASHTABLE) {
                        if (strcmp(node->as.assign.left->as.string_val, "%") == 0) {
                            snprintf(name_buf, sizeof(name_buf), "%s", node->as.assign.left->as.string_val);
                        } else {
                            snprintf(name_buf, sizeof(name_buf), "%%%s", node->as.assign.left->as.string_val);
                        }
                    } else {
                        snprintf(name_buf, sizeof(name_buf), "%s", node->as.assign.left->as.string_val);
                    }
                    uint32_t slen = (uint32_t)strlen(name_buf);
                    
                    if (node->as.assign.op.type != SLP_TOKEN_EQUAL) {
                        named_variable(compiler, name_buf, slen, false, line);
                        compile_expr(compiler, node->as.assign.right);
                        switch (node->as.assign.op.type) {
                            case SLP_TOKEN_PLUSEQUAL: emit_byte(compiler, OP_ADD, line); break;
                            case SLP_TOKEN_MINUSEQUAL: emit_byte(compiler, OP_SUBTRACT, line); break;
                            case SLP_TOKEN_TIMESEQUAL: emit_byte(compiler, OP_MULTIPLY, line); break;
                            case SLP_TOKEN_DIVEQUAL: emit_byte(compiler, OP_DIVIDE, line); break;
                            case SLP_TOKEN_CATEQUAL: emit_byte(compiler, OP_CONCAT, line); break;
                            case SLP_TOKEN_ANDEQUAL: emit_byte(compiler, OP_BIT_AND, line); break;
                            case SLP_TOKEN_OREQUAL: emit_byte(compiler, OP_BIT_OR, line); break;
                            case SLP_TOKEN_XOREQUAL: emit_byte(compiler, OP_BIT_XOR, line); break;
                            case SLP_TOKEN_LSHIFTEQUAL: emit_byte(compiler, OP_LSHIFT, line); break;
                            case SLP_TOKEN_RSHIFTEQUAL: emit_byte(compiler, OP_RSHIFT, line); break;
                            case SLP_TOKEN_EXPEQUAL: emit_byte(compiler, OP_POWER, line); break;
                            default: break;
                        }
                    } else {
                        compile_expr(compiler, node->as.assign.right);
                    }
                    named_variable(compiler, name_buf, slen, true, line);
                }
                break;
            }
            case SLP_AST_INDEX: {
                uint8_t kind =
                    index_autovivify_kind(node->as.assign.left);
                compile_assignment_container(
                    compiler,
                    node->as.assign.left->as.index.container,
                    kind, line);
                compile_expr(compiler, node->as.assign.left->as.index.element);
                if (node->as.assign.op.type != SLP_TOKEN_EQUAL) {
                    emit_byte(compiler, OP_DUP2, line);
                    emit_byte(compiler, OP_INDEX_GET, line);
                }
                compile_expr(compiler, node->as.assign.right);
                switch (node->as.assign.op.type) {
                case SLP_TOKEN_PLUSEQUAL:
                    emit_byte(compiler, OP_ADD, line);
                    break;
                case SLP_TOKEN_MINUSEQUAL:
                    emit_byte(compiler, OP_SUBTRACT, line);
                    break;
                case SLP_TOKEN_TIMESEQUAL:
                    emit_byte(compiler, OP_MULTIPLY, line);
                    break;
                case SLP_TOKEN_DIVEQUAL:
                    emit_byte(compiler, OP_DIVIDE, line);
                    break;
                case SLP_TOKEN_CATEQUAL:
                    emit_byte(compiler, OP_CONCAT, line);
                    break;
                case SLP_TOKEN_ANDEQUAL:
                    emit_byte(compiler, OP_BIT_AND, line);
                    break;
                case SLP_TOKEN_OREQUAL:
                    emit_byte(compiler, OP_BIT_OR, line);
                    break;
                case SLP_TOKEN_XOREQUAL:
                    emit_byte(compiler, OP_BIT_XOR, line);
                    break;
                case SLP_TOKEN_LSHIFTEQUAL:
                    emit_byte(compiler, OP_LSHIFT, line);
                    break;
                case SLP_TOKEN_RSHIFTEQUAL:
                    emit_byte(compiler, OP_RSHIFT, line);
                    break;
                case SLP_TOKEN_EXPEQUAL:
                    emit_byte(compiler, OP_POWER, line);
                    break;
                default:
                    break;
                }
                emit_byte(compiler, OP_INDEX_SET, line);
                break;
            }
            case SLP_AST_LVALUE_TUPLE: {
                bool compound =
                    node->as.assign.op.type != SLP_TOKEN_EQUAL;
                SlpOpcode compound_opcode =
                    assignment_operation_opcode(
                        node->as.assign.op.type);
                compile_expr(compiler, node->as.assign.right);
                for (size_t i = 0; i < node->as.assign.left->as.block.count; i++) {
                    SlpASTNode *element = node->as.assign.left->as.block.statements[i];
                    if (element->type == SLP_AST_SCALAR || element->type == SLP_AST_ARRAY ||
                        element->type == SLP_AST_HASHTABLE || element->type == SLP_AST_IDENTIFIER) {
                        if (element->as.string_val) {
                            char name_buf[256];
                            if (element->type == SLP_AST_SCALAR) {
                                snprintf(name_buf, sizeof(name_buf), "$%s", element->as.string_val);
                            } else if (element->type == SLP_AST_ARRAY) {
                                if (strcmp(element->as.string_val, "@") == 0) {
                                    snprintf(name_buf, sizeof(name_buf), "%s", element->as.string_val);
                                } else {
                                    snprintf(name_buf, sizeof(name_buf), "@%s", element->as.string_val);
                                }
                            } else if (element->type == SLP_AST_HASHTABLE) {
                                if (strcmp(element->as.string_val, "%") == 0) {
                                    snprintf(name_buf, sizeof(name_buf), "%s", element->as.string_val);
                                } else {
                                    snprintf(name_buf, sizeof(name_buf), "%%%s", element->as.string_val);
                                }
                            } else {
                                snprintf(name_buf, sizeof(name_buf), "%s", element->as.string_val);
                            }
                            uint32_t slen = (uint32_t)strlen(name_buf);

                            emit_byte(compiler, OP_DUP, line);
                            if (compound &&
                                node->as.assign.left->as.block.count == 1) {
                                named_variable(
                                    compiler, name_buf, slen,
                                    false, line);
                                emit_byte(
                                    compiler,
                                    OP_TUPLE_COMPOUND, line);
                                emit_byte(
                                    compiler,
                                    (uint8_t)compound_opcode,
                                    line);
                            } else {
                                emit_byte(compiler, OP_PUSH_CONST, line);
                                emit_short(compiler, make_constant(compiler, SLP_NUM_VAL((double)i)), line);
                                emit_byte(compiler, OP_TUPLE_GET, line);
                                if (compound) {
                                    named_variable(
                                        compiler, name_buf, slen,
                                        false, line);
                                    emit_byte(
                                        compiler, OP_SWAP, line);
                                    emit_byte(
                                        compiler,
                                        (uint8_t)compound_opcode,
                                        line);
                                }
                            }
                            named_variable(compiler, name_buf, slen, true, line);
                            emit_byte(compiler, OP_POP, line);
                        }
                    }
                }
                break;
            }
            default:
                break;
            }
        }
        break;
    }
    case SLP_AST_IF: {
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
    case SLP_AST_WHILE: {
        int outer_loop_start = compiler->loop_start;
        int outer_loop_continue = compiler->loop_continue_target;
        int outer_loop_exit = compiler->loop_exit_jump;
        int outer_loop_depth = compiler->loop_scope_depth;
        int outer_loop_try_depth = compiler->loop_try_depth;
        int outer_break_count = compiler->break_jump_count;
        int outer_continue_count = compiler->continue_jump_count;
        compiler->loop_start = current_chunk(compiler)->count;
        compiler->loop_continue_target = compiler->loop_start;
        compiler->loop_scope_depth = compiler->scope_depth;
        compiler->loop_try_depth = compiler->try_depth;
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
        compiler->loop_try_depth = outer_loop_try_depth;
        compiler->break_jump_count = outer_break_count;
        compiler->continue_jump_count = outer_continue_count;
        break;
    }
    case SLP_AST_FOR: {
        begin_scope(compiler);
        for (size_t i = 0; i < node->as.for_stmt.init_count; i++) {
            compile_node(compiler, node->as.for_stmt.initializer[i]);
            emit_byte(
                compiler, OP_POP,
                node->as.for_stmt.initializer[i]->line);
        }
        int outer_loop_start = compiler->loop_start;
        int outer_loop_continue = compiler->loop_continue_target;
        int outer_loop_exit = compiler->loop_exit_jump;
        int outer_loop_depth = compiler->loop_scope_depth;
        int outer_loop_try_depth = compiler->loop_try_depth;
        int outer_break_count = compiler->break_jump_count;
        int outer_continue_count = compiler->continue_jump_count;
        compiler->loop_start = current_chunk(compiler)->count;
        compiler->loop_scope_depth = compiler->scope_depth;
        compiler->loop_try_depth = compiler->try_depth;
        compiler->break_jump_count = 0;
        compiler->continue_jump_count = 0;
        if (node->as.for_stmt.condition) {
            compile_expr(compiler, node->as.for_stmt.condition);
            int exit_jump = emit_jump(compiler, OP_JUMP_IF_FALSE, line);
            compiler->loop_exit_jump = exit_jump;
            emit_byte(compiler, OP_POP, line);
            compile_expr(compiler, node->as.for_stmt.body);
            int inc_start = current_chunk(compiler)->count;
            for (size_t i = 0; i < node->as.for_stmt.inc_count; i++) {
                compile_node(compiler, node->as.for_stmt.increment[i]);
                emit_byte(
                    compiler, OP_POP,
                    node->as.for_stmt.increment[i]->line);
            }
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
            for (size_t i = 0; i < node->as.for_stmt.inc_count; i++) {
                compile_node(compiler, node->as.for_stmt.increment[i]);
                emit_byte(
                    compiler, OP_POP,
                    node->as.for_stmt.increment[i]->line);
            }
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
        compiler->loop_try_depth = outer_loop_try_depth;
        compiler->break_jump_count = outer_break_count;
        compiler->continue_jump_count = outer_continue_count;
        end_scope(compiler);
        break;
    }
    case SLP_AST_FOREACH: {
        begin_scope(compiler);
        compile_expr(compiler, node->as.foreach.generator);
        emit_byte(compiler, OP_PUSH_CONST, line);
        emit_short(compiler, make_constant(compiler, SLP_NUM_VAL(0)), line);

        int outer_loop_start = compiler->loop_start;
        int outer_loop_continue = compiler->loop_continue_target;
        int outer_loop_exit = compiler->loop_exit_jump;
        int outer_loop_depth = compiler->loop_scope_depth;
        int outer_loop_try_depth = compiler->loop_try_depth;
        int outer_break_count = compiler->break_jump_count;
        int outer_continue_count = compiler->continue_jump_count;
        compiler->loop_start = current_chunk(compiler)->count;
        compiler->loop_continue_target = compiler->loop_start;
        compiler->loop_scope_depth = compiler->scope_depth;
        compiler->loop_try_depth = compiler->try_depth;
        compiler->break_jump_count = 0;
        compiler->continue_jump_count = 0;

        int exit_jump = emit_jump(
            compiler,
            node->as.foreach.index
                ? OP_FOREACH_NEXT
                : OP_FOREACH_NEXT_VALUE,
            line);
        compiler->loop_exit_jump = exit_jump;

        if (node->as.foreach.index) {
            char val_buf[256];
            snprintf(val_buf, sizeof(val_buf), "$%s", node->as.foreach.value);
            SlpObjString *vname = intern_str(compiler, val_buf, (uint32_t)strlen(val_buf));
            emit_byte(compiler, OP_STORE_GLOBAL, line);
            emit_short(compiler, make_constant(compiler, SLP_OBJ_VAL(vname)), line);
            emit_byte(compiler, OP_POP, line);

            char idx_buf[256];
            snprintf(idx_buf, sizeof(idx_buf), "$%s", node->as.foreach.index);
            SlpObjString *iname = intern_str(compiler, idx_buf, (uint32_t)strlen(idx_buf));
            emit_byte(compiler, OP_STORE_GLOBAL, line);
            emit_short(compiler, make_constant(compiler, SLP_OBJ_VAL(iname)), line);
            emit_byte(compiler, OP_POP, line);
        } else {
            char val_buf[256];
            snprintf(val_buf, sizeof(val_buf), "$%s", node->as.foreach.value);
            SlpObjString *vname = intern_str(compiler, val_buf, (uint32_t)strlen(val_buf));
            emit_byte(compiler, OP_STORE_GLOBAL, line);
            emit_short(compiler, make_constant(compiler, SLP_OBJ_VAL(vname)), line);
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
        compiler->loop_try_depth = outer_loop_try_depth;
        compiler->break_jump_count = outer_break_count;
        compiler->continue_jump_count = outer_continue_count;
        end_scope(compiler);
        break;
    }
    case SLP_AST_CALL: {
        if (is_iff_call(node)) {
            compile_iff_call(compiler, node, line);
            break;
        }
        if (node->as.call.target &&
            node->as.call.target->type ==
                SLP_AST_IDENTIFIER &&
            node->as.call.target
                ->as.string_val) {
            const char *name =
                node->as.call.target
                    ->as.string_val;
            named_variable(
                compiler, name,
                (uint32_t)slp_utils_strlen(
                    name),
                false, line);
        } else {
            compile_expr(
                compiler,
                node->as.call.target);
        }
        int pushed_count = 0;
        for (size_t i = node->as.call.arg_count; i > 0; i--) {
            SlpASTNode *arg = node->as.call.args[i - 1];
            compile_node(compiler, arg);
            pushed_count += 1;
        }
        if (pushed_count > 255) {
            compiler_error(compiler, line, "Too many arguments in call.");
        }
        if (pushed_count > 1) {
            emit_byte(compiler, OP_REVERSE, line);
            emit_byte(compiler, (uint8_t)pushed_count, line);
        }
        bool named_call =
            node->as.call.target &&
            node->as.call.target->type == SLP_AST_IDENTIFIER &&
            node->as.call.target->as.string_val;
        emit_byte(
            compiler, named_call ? OP_CALL_NAMED : OP_CALL, line);
        emit_byte(compiler, (uint8_t)pushed_count, line);
        if (named_call) {
            SlpObjString *name = intern_str(
                compiler, node->as.call.target->as.string_val,
                (uint32_t)slp_utils_strlen(
                    node->as.call.target->as.string_val));
            emit_short(
                compiler,
                make_constant(compiler, SLP_OBJ_VAL(name)), line);
        }
        break;
    }
    case SLP_AST_RETURN:
        if (node->as.control.value)
            compile_expr(compiler, node->as.control.value);
        else
            emit_byte(compiler, OP_PUSH_NULL, line);
        emit_byte(compiler,
                  compiler->inline_body ? OP_INLINE_RETURN : OP_RETURN,
                  line);
        break;
    case SLP_AST_THROW:
        compile_expr(compiler, node->as.control.value);
        emit_byte(compiler, OP_THROW, line);
        break;
    case SLP_AST_ASSERT:
        if (assert_condition_is_predicate(node->as.control.value)) {
            compile_expr(compiler, node->as.control.value);
            if (!condition_is_explicit_predicate(
                    node->as.control.value))
                emit_truth_predicate(compiler, line);
        } else {
            emit_byte(compiler, OP_PUSH_FALSE, line);
        }

        {
            int success_jump =
                emit_jump(compiler, OP_JUMP_IF_TRUE, line);
            emit_byte(compiler, OP_POP, line);
            if (node->as.control.message) {
                compile_expr(compiler, node->as.control.message);
            } else {
                SlpObjString *message =
                    intern_str(compiler, "assertion failed", 16);
                emit_constant(
                    compiler, SLP_OBJ_VAL(message), line);
            }
            emit_byte(compiler, OP_ASSERT, line);
            int end_jump = emit_jump(compiler, OP_JUMP, line);
            patch_jump(compiler, success_jump);
            emit_byte(compiler, OP_POP, line);
            patch_jump(compiler, end_jump);
        }
        break;
    case SLP_AST_YIELD:
        if (node->as.control.value)
            compile_expr(compiler, node->as.control.value);
        else
            emit_byte(compiler, OP_PUSH_NULL, line);
        emit_byte(compiler, OP_YIELD, line);
        break;
    case SLP_AST_BREAK:
        if (compiler->loop_start >= 0) {
            for (int i = compiler->try_depth;
                 i > compiler->loop_try_depth; i--)
                emit_byte(compiler, OP_POP_HANDLER, line);
            int jmp = emit_jump(compiler, OP_JUMP, line);
            if (compiler->break_jump_count < 256)
                compiler->break_jumps[compiler->break_jump_count++] = jmp;
        }
        break;
    case SLP_AST_CONTINUE:
        if (compiler->loop_start >= 0) {
            for (int i = compiler->try_depth;
                 i > compiler->loop_try_depth; i--)
                emit_byte(compiler, OP_POP_HANDLER, line);
            int jmp = emit_jump(compiler, OP_JUMP, line);
            if (compiler->continue_jump_count < 256)
                compiler->continue_jumps[compiler->continue_jump_count++] = jmp;
        }
        break;
    case SLP_AST_HALT:
        emit_byte(compiler, OP_HALT, line);
        break;
    case SLP_AST_DONE:
        emit_byte(compiler, OP_DONE, line);
        break;
    case SLP_AST_TRY_CATCH: {
        int catch_offset = emit_jump(compiler, OP_PUSH_HANDLER, line);
        compiler->try_depth++;
        compile_expr(compiler, node->as.try_catch.body);
        compiler->try_depth--;
        emit_byte(compiler, OP_POP_HANDLER, line);
        int exit_jump = emit_jump(compiler, OP_JUMP, line);
        patch_jump(compiler, catch_offset);
        if (node->as.try_catch.value) {
            char catch_name[256];
            snprintf(
                catch_name, sizeof(catch_name), "$%s",
                node->as.try_catch.value);
            SlpObjString *vname = intern_str(compiler,
                catch_name, (uint32_t)slp_utils_strlen(catch_name));
            emit_byte(compiler, OP_STORE_CATCH, line);
            emit_short(compiler, make_constant(compiler, SLP_OBJ_VAL(vname)), line);
        }
        compile_expr(compiler, node->as.try_catch.handler);
        patch_jump(compiler, exit_jump);
        break;
    }
    case SLP_AST_ENV_BRIDGE: {
        SlpCompiler sub_compiler;
        init_compiler(&sub_compiler, compiler, compiler->vm, compiler->allocator);
        sub_compiler.inline_body =
            node->as.env_bridge.keyword &&
            strcmp(node->as.env_bridge.keyword, "inline") == 0;
        if (node->as.env_bridge.body)
            compile_node(&sub_compiler, node->as.env_bridge.body);
        SlpChunk *body_chunk = sub_compiler.function->chunk;
        int body_line = node->as.env_bridge.body
                            ? node->as.env_bridge.body->line
                            : line;
        sub_compiler.function->line_start = body_line;
        sub_compiler.function->line_end = body_line;
        if (body_chunk && body_chunk->count > 0) {
            int first = 0;
            int last = 0;
            for (int i = 0; i < body_chunk->count; i++) {
                int instruction_line = body_chunk->lines[i];
                if (instruction_line <= 0) continue;
                if (first == 0 || instruction_line < first)
                    first = instruction_line;
                if (instruction_line > last)
                    last = instruction_line;
            }
            if (first > 0) sub_compiler.function->line_start = first;
            if (last > 0) sub_compiler.function->line_end = last;
        }
        emit_return(&sub_compiler, line);
        // Propagate compile errors from the nested function up to the parent,
        // otherwise slp_compile_ex's top-level had_error check would miss them.
        if (sub_compiler.had_error) {
            compiler_error(compiler, sub_compiler.error_line, sub_compiler.error_message);
        }
        if (node->as.env_bridge.keyword) {
            SlpObjString *kw = intern_str(&sub_compiler, node->as.env_bridge.keyword,
                (uint32_t)slp_utils_strlen(node->as.env_bridge.keyword));
            sub_compiler.function->name = kw;
        }
        SlpObjFunction *fn = sub_compiler.function;
        emit_byte(compiler, OP_CLOSURE, line);
        emit_short(compiler, make_constant(compiler, SLP_OBJ_VAL(fn)), line);
        for (int i = 0; i < fn->upvalue_count; i++) {
            emit_byte(compiler, sub_compiler.upvalues[i].is_local ? 1 : 0, line);
            emit_byte(compiler, sub_compiler.upvalues[i].index, line);
        }
        if (node->as.env_bridge.keyword && node->as.env_bridge.identifier) {
            SlpObjString *kw = intern_str(compiler, node->as.env_bridge.keyword,
                (uint32_t)slp_utils_strlen(node->as.env_bridge.keyword));
            emit_byte(compiler, OP_BRIDGE_REGISTER, line);
            emit_short(compiler, make_constant(compiler, SLP_OBJ_VAL(kw)), line);
            SlpObjString *name = intern_str(compiler, node->as.env_bridge.identifier,
                (uint32_t)slp_utils_strlen(node->as.env_bridge.identifier));
            emit_short(compiler, make_constant(compiler, SLP_OBJ_VAL(name)), line);
        }
        break;
    }
    case SLP_AST_IMPORT: {
        if (node->as.import_stmt.target) {
            SlpObjString *target = intern_str(compiler, node->as.import_stmt.target,
                (uint32_t)slp_utils_strlen(node->as.import_stmt.target));
            emit_byte(compiler, OP_IMPORT, line);
            emit_short(compiler, make_constant(compiler, SLP_OBJ_VAL(target)), line);
            if (node->as.import_stmt.path) {
                SlpObjString *path = intern_str(compiler, node->as.import_stmt.path,
                    (uint32_t)slp_utils_strlen(node->as.import_stmt.path));
                emit_short(compiler, make_constant(compiler, SLP_OBJ_VAL(path)), line);
            } else {
                emit_short(compiler, 0, line);
            }
        }
        break;
    }
    case SLP_AST_INDEX:
        compile_expr(compiler, node->as.index.container);
        compile_expr(compiler, node->as.index.element);
        emit_byte(compiler, OP_INDEX_GET, line);
        break;
    case SLP_AST_OBJ_EXPR:
        if (node->as.obj_expr.target &&
            node->as.obj_expr.target->type ==
                SLP_AST_IDENTIFIER &&
            node->as.obj_expr.target
                ->as.string_val) {
            const char *name =
                node->as.obj_expr.target
                    ->as.string_val;
            named_variable(
                compiler, name,
                (uint32_t)slp_utils_strlen(
                    name),
                false, line);
        } else {
            compile_expr(
                compiler,
                node->as.obj_expr.target);
        }
        if (node->as.obj_expr.arg_count > 255) {
            compiler_error(compiler, line, "Too many arguments in object expression.");
        }
        if (node->as.obj_expr.message == NULL) {
            // Closure invocation: [$target arg1, arg2]
            for (size_t i = node->as.obj_expr.arg_count; i > 0; i--) {
                compile_node(compiler, node->as.obj_expr.args[i - 1]);
            }
            if (node->as.obj_expr.arg_count > 1) {
                emit_byte(compiler, OP_REVERSE, line);
                emit_byte(
                    compiler, (uint8_t)node->as.obj_expr.arg_count, line);
            }
            emit_byte(compiler, OP_CALL, line);
            emit_byte(compiler, (uint8_t)node->as.obj_expr.arg_count, line);
        } else {
            if (node->as.obj_expr.message->type == SLP_AST_IDENTIFIER && node->as.obj_expr.message->as.string_val) {
                SlpObjString *str = intern_str(compiler, node->as.obj_expr.message->as.string_val,
                    (uint32_t)slp_utils_strlen(node->as.obj_expr.message->as.string_val));
                emit_byte(compiler, OP_PUSH_CONST, line);
                emit_short(compiler, make_constant(compiler, SLP_OBJ_VAL(str)), line);
            } else {
                compile_expr(compiler, node->as.obj_expr.message);
            }
            for (size_t i = node->as.obj_expr.arg_count; i > 0; i--) {
                compile_node(compiler, node->as.obj_expr.args[i - 1]);
            }
            if (node->as.obj_expr.arg_count > 1) {
                emit_byte(compiler, OP_REVERSE, line);
                emit_byte(
                    compiler, (uint8_t)node->as.obj_expr.arg_count, line);
            }
            emit_byte(compiler, OP_OBJ_EXPR, line);
            emit_byte(compiler, (uint8_t)node->as.obj_expr.arg_count, line);
        }
        break;
    case SLP_AST_BACKTICK: {
        if (node->as.string_val) {
            SlpObjString *cmd = intern_str(compiler, node->as.string_val,
                (uint32_t)slp_utils_strlen(node->as.string_val));
            emit_byte(compiler, OP_BACKTICK, line);
            emit_short(compiler, make_constant(compiler, SLP_OBJ_VAL(cmd)), line);
        }
        break;
    }
    case SLP_AST_CLASS_LITERAL: {
        if (node->as.string_val) {
            SlpObjString *cls = intern_str(compiler, node->as.string_val,
                (uint32_t)slp_utils_strlen(node->as.string_val));
            emit_byte(compiler, OP_CLASS_LITERAL, line);
            emit_short(compiler, make_constant(compiler, SLP_OBJ_VAL(cls)), line);
        }
        break;
    }
    case SLP_AST_ADDRESS: {
        if (node->as.string_val) {
            SlpObjString *addr = intern_str(compiler, node->as.string_val,
                (uint32_t)slp_utils_strlen(node->as.string_val));
            emit_byte(compiler, OP_ADDRESS, line);
            emit_short(compiler, make_constant(compiler, SLP_OBJ_VAL(addr)), line);
        }
        break;
    }
    case SLP_AST_LOCAL:
        if (node->as.control.value) {
            compile_expr(compiler, node->as.control.value);
            emit_byte(compiler, OP_LOCAL, line);
        } else {
            emit_byte(compiler, OP_PUSH_NULL, line);
        }
        break;
    case SLP_AST_THIS:
        if (node->as.control.value) {
            compile_expr(compiler, node->as.control.value);
            emit_byte(compiler, OP_THIS, line);
        } else {
            emit_byte(compiler, OP_PUSH_NULL, line);
        }
        break;
    case SLP_AST_CALLCC:
        if (node->as.control.value)
            compile_expr(compiler, node->as.control.value);
        emit_byte(compiler, OP_CALLCC, line);
        emit_byte(compiler, node->as.control.value ? 1 : 0, line);
        break;
    case SLP_AST_LVALUE_TUPLE:
        for (size_t i = 0; i < node->as.block.count; i++)
            compile_node(compiler, node->as.block.statements[i]);
        if (node->as.block.count > 255) {
            compiler_error(compiler, line, "Too many elements in tuple assignment.");
        }
        emit_byte(compiler, OP_UNPACK_TUPLE, line);
        emit_byte(compiler, (uint8_t)node->as.block.count, line);
        break;
    case SLP_AST_ASSIGN_LOOP: {
        int outer_loop_start = compiler->loop_start;
        int outer_loop_continue = compiler->loop_continue_target;
        int outer_loop_exit = compiler->loop_exit_jump;
        int outer_loop_depth = compiler->loop_scope_depth;
        int outer_loop_try_depth = compiler->loop_try_depth;
        int outer_break_count = compiler->break_jump_count;
        int outer_continue_count = compiler->continue_jump_count;
        compiler->loop_start = current_chunk(compiler)->count;
        compiler->loop_continue_target = compiler->loop_start;
        compiler->loop_scope_depth = compiler->scope_depth;
        compiler->loop_try_depth = compiler->try_depth;
        compiler->break_jump_count = 0;
        compiler->continue_jump_count = 0;

        compile_expr(compiler, node->as.assign_loop.generator);
        char name_buf[256];
        snprintf(name_buf, sizeof(name_buf), "$%s",
                 node->as.assign_loop.value);
        named_variable(compiler, name_buf,
                       (uint32_t)slp_utils_strlen(name_buf), true, line);
        /*
         * Assignment-form while loops consume streams. Sleep's empty scalar
         * ($null) ends the stream, while valid false-looking values such as
         * integer 0 and an empty line still enter the loop body.
         */
        int exit_jump = emit_jump(compiler, OP_JUMP_IF_NULL, line);
        compiler->loop_exit_jump = exit_jump;
        emit_byte(compiler, OP_POP, line);
        compile_expr(compiler, node->as.assign_loop.body);
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
        compiler->loop_try_depth = outer_loop_try_depth;
        compiler->break_jump_count = outer_break_count;
        compiler->continue_jump_count = outer_continue_count;
        break;
    }
    case SLP_AST_ARG:
    case SLP_AST_KV_PAIR: {
        /* Pass-by-name (\$x) is parsed as an address-shaped argument. It is
           exactly the same as $x => $x, including retaining the scalar cell. */
        if (!node->as.arg.name && node->as.arg.value &&
            node->as.arg.value->type == SLP_AST_ADDRESS &&
            node->as.arg.value->as.string_val &&
            (node->as.arg.value->as.string_val[0] == '$' ||
             node->as.arg.value->as.string_val[0] == '@' ||
             node->as.arg.value->as.string_val[0] == '%')) {
            const char *name = node->as.arg.value->as.string_val;
            SlpObjString *str = intern_str(compiler, name,
                (uint32_t)slp_utils_strlen(name));
            emit_byte(compiler, OP_PUSH_CONST, line);
            emit_short(compiler, make_constant(compiler, SLP_OBJ_VAL(str)), line);
            reference_variable(compiler, name,
                (uint32_t)slp_utils_strlen(name), line);
            emit_byte(compiler, OP_BUILD_KEY_VALUE, line);
            break;
        }

        if (node->as.arg.name) {
            SlpASTNode *name_node = node->as.arg.name;
            char name_buf[256];
            if (variable_node_name(name_node, name_buf, sizeof(name_buf))) {
                SlpObjString *str = intern_str(compiler, name_buf,
                    (uint32_t)slp_utils_strlen(name_buf));
                emit_byte(compiler, OP_PUSH_CONST, line);
                emit_short(compiler, make_constant(compiler, SLP_OBJ_VAL(str)), line);
            } else if (name_node->type == SLP_AST_IDENTIFIER &&
                       name_node->as.string_val) {
                char name_buf[256];
                snprintf(name_buf, sizeof(name_buf), "%s",
                         name_node->as.string_val);
                SlpObjString *str = intern_str(compiler, name_buf,
                    (uint32_t)slp_utils_strlen(name_buf));
                emit_byte(compiler, OP_PUSH_CONST, line);
                emit_short(compiler, make_constant(compiler, SLP_OBJ_VAL(str)), line);
            } else {
                compile_expr(compiler, name_node);
            }
        }

        char value_name[256];
        if (variable_node_name(node->as.arg.value, value_name,
                               sizeof(value_name))) {
            reference_variable(compiler, value_name,
                               (uint32_t)slp_utils_strlen(value_name), line);
        } else {
            compile_expr(compiler, node->as.arg.value);
        }
        if (node->as.arg.name)
            emit_byte(compiler, OP_BUILD_KEY_VALUE, line);
        break;
    }
    case SLP_AST_NOP:
        emit_byte(compiler, OP_NOP, line);
        break;
    default:
        break;
    }
}

static void named_variable(SlpCompiler *compiler, const char *name, uint32_t len, bool assign, int line) {
    int arg = resolve_local(compiler, name, len);
    SlpOpcode get_op, set_op;
    if (arg != -1) {
        if (arg < 8) {
            get_op = (SlpOpcode)(OP_LOAD_LOCAL_0 + arg);
            set_op = (SlpOpcode)(OP_STORE_LOCAL_0 + arg);
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
        SlpObjString *str = intern_str(compiler, name, len);
        arg = slp_chunk_add_constant(current_chunk(compiler), SLP_OBJ_VAL(str));
    }

    if (assign) {
        if (set_op >= OP_STORE_LOCAL_0 && set_op <= OP_STORE_LOCAL_7) {
            emit_byte(compiler, (uint8_t)set_op, line);
        } else if (set_op == OP_STORE_LOCAL) {
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
            emit_byte(compiler, (uint8_t)get_op, line);
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

static void reference_variable(SlpCompiler *compiler, const char *name,
                               uint32_t len, int line) {
    int index = resolve_local(compiler, name, len);
    if (index != -1) {
        emit_byte(compiler, OP_REFERENCE_LOCAL, line);
        emit_byte(compiler, (uint8_t)index, line);
        return;
    }

    index = resolve_upvalue(compiler, name, len);
    if (index != -1) {
        emit_byte(compiler, OP_REFERENCE_UPVALUE, line);
        emit_byte(compiler, (uint8_t)index, line);
        return;
    }

    SlpObjString *str = intern_str(compiler, name, len);
    int constant = slp_chunk_add_constant(current_chunk(compiler),
                                          SLP_OBJ_VAL(str));
    if (constant < 0 || constant > UINT16_MAX) {
        compiler_error(compiler, line, "Too many constants in one chunk.");
        constant = 0;
    }
    emit_byte(compiler, OP_REFERENCE_GLOBAL, line);
    emit_short(compiler, (uint16_t)constant, line);
}

SlpObjFunction *slp_compile(SlpVM *vm, SlpASTNode *ast, SlpAllocator *allocator) {
    return slp_compile_ex(vm, ast, false, allocator);
}

SlpObjFunction *slp_compile_ex(SlpVM *vm, SlpASTNode *ast, bool repl_mode, SlpAllocator *allocator) {
    SlpCompiler compiler;
    if (vm) {
        vm->compile_error_message[0] =
            '\0';
        vm->compile_error_line = 0;
    }
    init_compiler(&compiler, NULL, vm, allocator);
    compiler.repl_mode = repl_mode;
    if (!compiler.function) return NULL;
    compile_node(&compiler, ast);
    emit_return(&compiler, ast ? ast->line : 0);
    if (compiler.had_error) {
        if (vm) {
            snprintf(
                vm->compile_error_message,
                sizeof(
                    vm->compile_error_message),
                "%s",
                compiler.error_message
                    ? compiler.error_message
                    : "Compile error");
            vm->compile_error_line =
                compiler.error_line;
        }
        return NULL;
    }
    return compiler.function;
}
