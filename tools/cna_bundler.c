#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifdef _WIN32
#include <windows.h>
#define realpath(N, R) _fullpath((R), (N), PATH_MAX)
#endif

#include "slp_common.h"
#include "slp_lexer.h"
#include "slp_parser.h"
#include "slp_ast.h"

// Basic memory allocator implementation
static void* default_realloc(void* ptr, size_t new_size, void* user_data) {
    (void)user_data;
    if (new_size == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, new_size);
}

#include "utils.h"
#include <dirent.h>
#include <sys/stat.h>

static void find_entrypoint_recursive(const char* dir_path, const char* expected_name, char** best_match, long* max_size, bool* exact_match_found) {
    if (*exact_match_found) return;
    
    DIR* dir = opendir(dir_path);
    if (!dir) return;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
        
        struct stat st;
        if (stat(path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                find_entrypoint_recursive(path, expected_name, best_match, max_size, exact_match_found);
            } else if (S_ISREG(st.st_mode)) {
                size_t len = strlen(entry->d_name);
                if (len > 4 && strcasecmp(entry->d_name + len - 4, ".cna") == 0) {
                    if (strcasecmp(entry->d_name, expected_name) == 0) {
                        if (*best_match) free(*best_match);
                        *best_match = strdup(path);
                        *exact_match_found = true;
                        break;
                    }
                    if (st.st_size > *max_size) {
                        *max_size = st.st_size;
                        if (*best_match) free(*best_match);
                        *best_match = strdup(path);
                    }
                }
            }
        }
    }
    closedir(dir);
}

static char* find_entrypoint(const char* project_dir, const char* project_name) {
    char* best_match = NULL;
    long max_size = -1;
    bool exact_match_found = false;
    
    char expected_name[256];
    snprintf(expected_name, sizeof(expected_name), "%s.cna", project_name);
    
    find_entrypoint_recursive(project_dir, expected_name, &best_match, &max_size, &exact_match_found);
    return best_match;
}

typedef struct {
    char project_name[256];
    char old_alias[256];
    char new_alias[256];
} RenameRule;

typedef struct {
    RenameRule* rules;
    size_t count;
    size_t capacity;
} RenameConfig;

static void parse_config(const char* filepath, RenameConfig* config) {
    FILE* f = fopen(filepath, "r");
    if (!f) {
        fprintf(stderr, "Warning: Could not open config file %s\n", filepath);
        return;
    }
    
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        
        char proj[256] = {0}, old_a[256] = {0}, new_a[256] = {0};
        char* colon = strchr(line, ':');
        if (!colon) continue;
        char* equals = strchr(colon, '=');
        if (!equals) continue;
        
        size_t proj_len = colon - line;
        size_t old_len = equals - (colon + 1);
        
        strncpy(proj, line, proj_len);
        strncpy(old_a, colon + 1, old_len);
        
        char* end = equals + 1;
        while (*end && *end != '\r' && *end != '\n') end++;
        strncpy(new_a, equals + 1, end - (equals + 1));
        
        if (config->count >= config->capacity) {
            config->capacity = config->capacity == 0 ? 16 : config->capacity * 2;
            config->rules = realloc(config->rules, config->capacity * sizeof(RenameRule));
        }
        
        strncpy(config->rules[config->count].project_name, proj, 255);
        strncpy(config->rules[config->count].old_alias, old_a, 255);
        strncpy(config->rules[config->count].new_alias, new_a, 255);
        config->count++;
    }
    fclose(f);
}

typedef struct {
    char name[256];
    char project[256];
} AliasInfo;

typedef struct {
    char** rename_subs;
    size_t rename_subs_count;
    
    char** rename_globals;
    size_t rename_globals_count;
    
    AliasInfo* alias_targets;
    size_t alias_targets_count;
    size_t alias_targets_capacity;
    
    char** local_vars; // Temporarily tracked per sub, but useful for stats
    size_t local_vars_count;
    
    char** sources;
    size_t sources_count;
    
    char** loaded_paths;
    size_t loaded_paths_count;
    
    RenameConfig* config;
    
    SlpAllocator* allocator;
} SymbolTable;
 
static void init_symbol_table(SymbolTable* table, SlpAllocator* allocator, RenameConfig* config) {
    table->rename_subs = NULL;
    table->rename_subs_count = 0;
    table->rename_globals = NULL;
    table->rename_globals_count = 0;
    table->alias_targets = NULL;
    table->alias_targets_count = 0;
    table->alias_targets_capacity = 0;
    table->local_vars = NULL;
    table->local_vars_count = 0;
    table->sources = NULL;
    table->sources_count = 0;
    table->loaded_paths = NULL;
    table->loaded_paths_count = 0;
    table->config = config;
    table->allocator = allocator;
}
 
static void add_string_to_list(char*** list, size_t* count, const char* str, SlpAllocator* allocator) {
    if (!str) return;
    for (size_t i = 0; i < *count; i++) {
        if (strcmp((*list)[i], str) == 0) {
            return;
        }
    }
    
    *list = allocator->reallocate(*list, sizeof(char*) * (*count + 1), allocator->user_data);
    size_t len = strlen(str);
    (*list)[*count] = allocator->reallocate(NULL, len + 1, allocator->user_data);
    strcpy((*list)[*count], str);
    (*count)++;
}
 
static void free_string_list(char** list, size_t count, SlpAllocator* allocator) {
    for (size_t i = 0; i < count; i++) {
        allocator->reallocate(list[i], 0, allocator->user_data);
    }
    if (list) {
        allocator->reallocate(list, 0, allocator->user_data);
    }
}
 
static void free_symbol_table(SymbolTable* table) {
    free_string_list(table->rename_subs, table->rename_subs_count, table->allocator);
    free_string_list(table->rename_globals, table->rename_globals_count, table->allocator);
    if (table->alias_targets) {
        table->allocator->reallocate(table->alias_targets, 0, table->allocator->user_data);
    }
    free_string_list(table->local_vars, table->local_vars_count, table->allocator);
    for (size_t i = 0; i < table->sources_count; i++) {
        free(table->sources[i]);
    }
    if (table->sources) {
        table->allocator->reallocate(table->sources, 0, table->allocator->user_data);
    }
    free_string_list(table->loaded_paths, table->loaded_paths_count, table->allocator);
}

static void add_alias_to_list(SymbolTable* table, const char* name, const char* project) {
    if (!name || !project) return;
    for (size_t i = 0; i < table->alias_targets_count; i++) {
        if (strcmp(table->alias_targets[i].name, name) == 0 && strcmp(table->alias_targets[i].project, project) == 0) {
            return;
        }
    }
    
    if (table->alias_targets_count >= table->alias_targets_capacity) {
        table->alias_targets_capacity = table->alias_targets_capacity == 0 ? 16 : table->alias_targets_capacity * 2;
        table->alias_targets = table->allocator->reallocate(table->alias_targets, sizeof(AliasInfo) * table->alias_targets_capacity, table->allocator->user_data);
    }
    
    strncpy(table->alias_targets[table->alias_targets_count].name, name, 255);
    strncpy(table->alias_targets[table->alias_targets_count].project, project, 255);
    table->alias_targets_count++;
}

static void print_symbols(const char* prefix, SymbolTable* table) {
    printf("--- Symbols for %s ---\n", prefix);
    printf("Subroutines (%zu):\n", table->rename_subs_count);
    for (size_t i = 0; i < table->rename_subs_count; i++) {
        printf("  %s\n", table->rename_subs[i]);
    }
    printf("Globals (%zu):\n", table->rename_globals_count);
    for (size_t i = 0; i < table->rename_globals_count; i++) {
        printf("  %s\n", table->rename_globals[i]);
    }
    printf("Aliases/Commands (%zu):\n", table->alias_targets_count);
    for (size_t i = 0; i < table->alias_targets_count; i++) {
        printf("  %s (from %s)\n", table->alias_targets[i].name, table->alias_targets[i].project);
    }
    printf("\n");
}

static void extract_local_vars(SymbolTable* symbols, const char* local_str) {
    if (!local_str) return;
    char* copy = symbols->allocator->reallocate(NULL, strlen(local_str) + 1, symbols->allocator->user_data);
    strcpy(copy, local_str);
    
    char* token = strtok(copy, " \t\r\n");
    while (token != NULL) {
        if (token[0] == '$' || token[0] == '@' || token[0] == '%') {
            add_string_to_list(&symbols->local_vars, &symbols->local_vars_count, token, symbols->allocator);
        }
        token = strtok(NULL, " \t\r\n");
    }
    
    symbols->allocator->reallocate(copy, 0, symbols->allocator->user_data);
}

static void scan_pass(SlpASTNode* node, SymbolTable* symbols, bool is_top_level, const char* project_name) {
    if (!node) return;
    
    switch (node->type) {
        case SLP_AST_SCRIPT:
        case SLP_AST_BLOCK: {
            SlpASTNode** children;
            size_t count;
            slp_ast_get_children(node, &children, &count, symbols->allocator);
            for (size_t i = 0; i < count; i++) {
                scan_pass(children[i], symbols, is_top_level, project_name);
            }
            slp_ast_free_children(children, symbols->allocator);
            break;
        }
        case SLP_AST_ENV_BRIDGE: {
            const char* keyword = slp_ast_get_env_bridge_keyword(node);
            const char* id = slp_ast_get_env_bridge_id(node);
            
            if (keyword && id) {
                if (strcmp(keyword, "sub") == 0) {
                    add_string_to_list(&symbols->rename_subs, &symbols->rename_subs_count, id, symbols->allocator);
                } else if (strcmp(keyword, "alias") == 0) {
                    add_alias_to_list(symbols, id, project_name);
                }
            }
            
            SlpASTNode** children;
            size_t count;
            slp_ast_get_children(node, &children, &count, symbols->allocator);
            for (size_t i = 0; i < count; i++) {
                scan_pass(children[i], symbols, false, project_name);
            }
            slp_ast_free_children(children, symbols->allocator);
            break;
        }
        case SLP_AST_ASSIGNMENT: {
            if (is_top_level) {
                SlpASTNode** children;
                size_t count;
                slp_ast_get_children(node, &children, &count, symbols->allocator);
                if (count >= 1 && children[0]) {
                    if (children[0]->type == SLP_AST_SCALAR || 
                        children[0]->type == SLP_AST_ARRAY || 
                        children[0]->type == SLP_AST_HASHTABLE) {
                        const char* var_name = slp_ast_get_string(children[0]);
                        if (var_name) {
                            add_string_to_list(&symbols->rename_globals, &symbols->rename_globals_count, var_name, symbols->allocator);
                        }
                    }
                }
                for(size_t i = 0; i < count; i++) {
                    scan_pass(children[i], symbols, false, project_name);
                }
                slp_ast_free_children(children, symbols->allocator);
            } else {
                SlpASTNode** children;
                size_t count;
                slp_ast_get_children(node, &children, &count, symbols->allocator);
                for (size_t i = 0; i < count; i++) {
                    scan_pass(children[i], symbols, false, project_name);
                }
                slp_ast_free_children(children, symbols->allocator);
            }
            break;
        }
        case SLP_AST_CALL: {
            if (node->as.call.target && node->as.call.target->type == SLP_AST_IDENTIFIER) {
                const char* func_name = slp_ast_get_string(node->as.call.target);
                if (func_name && node->as.call.arg_count > 0 && node->as.call.args[0]) {
                    SlpASTNode* arg0 = node->as.call.args[0];
                    if (arg0->type == SLP_AST_ARG) {
                        arg0 = arg0->as.arg.value;
                    }
                    if (arg0 && (arg0->type == SLP_AST_STRING || arg0->type == SLP_AST_LITERAL)) {
                        if (strcmp(func_name, "beacon_command_register") == 0 ||
                            strcmp(func_name, "beacon_command_register_options") == 0 ||
                            strcmp(func_name, "beacon_command_register_file") == 0 ||
                            strcmp(func_name, "aggressor_command_register") == 0 ||
                            strcmp(func_name, "alias_register") == 0 ||
                            strcmp(func_name, "fireAlias") == 0) {
                            const char* cmd_name = slp_ast_get_string(arg0);
                            if (cmd_name) {
                                add_alias_to_list(symbols, cmd_name, project_name);
                            }
                        } else if (strcmp(func_name, "local") == 0) {
                            const char* local_vars = slp_ast_get_string(arg0);
                            if (local_vars) {
                                extract_local_vars(symbols, local_vars);
                            }
                        }
                    }
                }
            }
            
            if (node->as.call.target) {
                scan_pass(node->as.call.target, symbols, false, project_name);
            }
            for (size_t i = 0; i < node->as.call.arg_count; i++) {
                scan_pass(node->as.call.args[i], symbols, false, project_name);
            }
            break;
        }
        default: {
            SlpASTNode** children;
            size_t count;
            slp_ast_get_children(node, &children, &count, symbols->allocator);
            for (size_t i = 0; i < count; i++) {
                scan_pass(children[i], symbols, false, project_name);
            }
            slp_ast_free_children(children, symbols->allocator);
            break;
        }
    }
}

static bool string_in_list(char** list, size_t count, const char* str) {
    if (!str) return false;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(list[i], str) == 0) {
            return true;
        }
    }
    return false;
}

static void apply_prefix(SlpASTNode* node, const char* prefix, SlpAllocator* allocator) {
    const char* old_str = slp_ast_get_string(node);
    if (!old_str) return;
    
    bool has_sigil = (old_str[0] == '$' || old_str[0] == '@' || old_str[0] == '%');
    
    size_t len = strlen(old_str) + strlen(prefix) + 1;
    char* new_str = allocator->reallocate(NULL, len, allocator->user_data);
    
    if (has_sigil) {
        new_str[0] = old_str[0];
        new_str[1] = '\0';
        strcpy(new_str + 1, prefix);
        strcat(new_str, old_str + 1);
    } else {
        strcpy(new_str, prefix);
        strcat(new_str, old_str);
    }
    slp_ast_set_string_val(node, new_str, allocator);
    allocator->reallocate(new_str, 0, allocator->user_data);
}

static const char* get_renamed_alias(SymbolTable* symbols, const char* project_name, const char* alias) {
    if (!symbols->config) return alias;
    for (size_t i = 0; i < symbols->config->count; i++) {
        if (strcmp(symbols->config->rules[i].project_name, project_name) == 0 &&
            strcmp(symbols->config->rules[i].old_alias, alias) == 0) {
            return symbols->config->rules[i].new_alias;
        }
    }
    return alias;
}

static void rewrite_pass(SlpASTNode* node, SymbolTable* symbols, const char* prefix, const char* rel_path, const char* project_name, char*** active_locals, size_t* active_locals_count) {
    if (!node) return;
    
    switch (node->type) {
        case SLP_AST_ENV_BRIDGE: {
            const char* keyword = slp_ast_get_env_bridge_keyword(node);
            const char* id = slp_ast_get_env_bridge_id(node);
            
            if (keyword && id && strcmp(keyword, "sub") == 0) {
                if (string_in_list(symbols->rename_subs, symbols->rename_subs_count, id)) {
                    size_t len = strlen(id) + strlen(prefix) + 1;
                    char* new_id = symbols->allocator->reallocate(NULL, len, symbols->allocator->user_data);
                    strcpy(new_id, prefix);
                    strcat(new_id, id);
                    slp_ast_set_env_bridge_id(node, new_id, symbols->allocator);
                    symbols->allocator->reallocate(new_id, 0, symbols->allocator->user_data);
                }
            } else if (keyword && id && strcmp(keyword, "alias") == 0) {
                const char* new_alias = get_renamed_alias(symbols, project_name, id);
                if (new_alias && strcmp(new_alias, id) != 0) {
                    char* new_id = symbols->allocator->reallocate(NULL, strlen(new_alias) + 1, symbols->allocator->user_data);
                    strcpy(new_id, new_alias);
                    slp_ast_set_env_bridge_id(node, new_id, symbols->allocator);
                    symbols->allocator->reallocate(new_id, 0, symbols->allocator->user_data);
                }
            }
            
            size_t old_locals_count = active_locals ? *active_locals_count : 0;
            char** current_locals = active_locals ? *active_locals : NULL;
            size_t current_locals_count = old_locals_count;
            
            SlpASTNode** children;
            size_t count;
            slp_ast_get_children(node, &children, &count, symbols->allocator);
            
            for (size_t i = 0; i < count; i++) {
                if (children[i] && children[i]->type == SLP_AST_CALL) {
                    SlpASTNode* call = children[i];
                    if (call->as.call.target && call->as.call.target->type == SLP_AST_IDENTIFIER) {
                        const char* fn = slp_ast_get_string(call->as.call.target);
                        if (fn && strcmp(fn, "local") == 0 && call->as.call.arg_count > 0) {
                            SlpASTNode* arg0 = call->as.call.args[0];
                            if (arg0->type == SLP_AST_ARG) arg0 = arg0->as.arg.value;
                            if (arg0 && (arg0->type == SLP_AST_STRING || arg0->type == SLP_AST_LITERAL)) {
                                const char* lstr = slp_ast_get_string(arg0);
                                if (lstr) {
                                    char* copy = symbols->allocator->reallocate(NULL, strlen(lstr) + 1, symbols->allocator->user_data);
                                    strcpy(copy, lstr);
                                    char* token = strtok(copy, " \t\r\n");
                                    while (token) {
                                        add_string_to_list(&current_locals, &current_locals_count, token, symbols->allocator);
                                        token = strtok(NULL, " \t\r\n");
                                    }
                                    symbols->allocator->reallocate(copy, 0, symbols->allocator->user_data);
                                }
                            }
                        }
                    }
                }
            }
            
            for (size_t i = 0; i < count; i++) {
                rewrite_pass(children[i], symbols, prefix, rel_path, project_name, &current_locals, &current_locals_count);
            }
            
            if (active_locals) {
                for (size_t i = old_locals_count; i < current_locals_count; i++) {
                    symbols->allocator->reallocate(current_locals[i], 0, symbols->allocator->user_data);
                }
                if (current_locals_count > old_locals_count && old_locals_count == 0 && current_locals) {
                    symbols->allocator->reallocate(current_locals, 0, symbols->allocator->user_data);
                    current_locals = NULL;
                }
                *active_locals_count = old_locals_count;
                *active_locals = current_locals;
            }
            slp_ast_free_children(children, symbols->allocator);
            break;
        }
        case SLP_AST_CALL: {
            if (node->as.call.target && node->as.call.target->type == SLP_AST_IDENTIFIER) {
                const char* func_name = slp_ast_get_string(node->as.call.target);
                if (func_name) {
                    if (string_in_list(symbols->rename_subs, symbols->rename_subs_count, func_name)) {
                        apply_prefix(node->as.call.target, prefix, symbols->allocator);
                    } else if (strcmp(func_name, "beacon_command_register") == 0 ||
                               strcmp(func_name, "beacon_command_register_options") == 0 ||
                               strcmp(func_name, "beacon_command_register_file") == 0 ||
                               strcmp(func_name, "aggressor_command_register") == 0 ||
                               strcmp(func_name, "alias_register") == 0 ||
                               strcmp(func_name, "fireAlias") == 0) {
                        if (node->as.call.arg_count > 0 && node->as.call.args[0]) {
                            SlpASTNode* arg0 = node->as.call.args[0];
                            if (arg0->type == SLP_AST_ARG) arg0 = arg0->as.arg.value;
                            if (arg0 && (arg0->type == SLP_AST_STRING || arg0->type == SLP_AST_LITERAL)) {
                                const char* cmd_name = slp_ast_get_string(arg0);
                                if (cmd_name) {
                                    const char* new_alias = get_renamed_alias(symbols, project_name, cmd_name);
                                    if (new_alias && strcmp(new_alias, cmd_name) != 0) {
                                        char* new_str = symbols->allocator->reallocate(NULL, strlen(new_alias) + 1, symbols->allocator->user_data);
                                        strcpy(new_str, new_alias);
                                        slp_ast_set_string_val(arg0, new_str, symbols->allocator);
                                        symbols->allocator->reallocate(new_str, 0, symbols->allocator->user_data);
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
            if (node->as.call.target) {
                rewrite_pass(node->as.call.target, symbols, prefix, rel_path, project_name, active_locals, active_locals_count);
            }
            for (size_t i = 0; i < node->as.call.arg_count; i++) {
                rewrite_pass(node->as.call.args[i], symbols, prefix, rel_path, project_name, active_locals, active_locals_count);
            }
            break;
        }
        case SLP_AST_STRING: {
            const char* str = slp_ast_get_string(node);
            if (str && (strchr(str, '$') || strchr(str, '@') || strchr(str, '%'))) {
                // To avoid complex regex in C, we will just iterate over rename_globals
                // and replace occurrences of `$var` with `$Proj_var`.
                // A better implementation would parse the string, but this is a naive replace.
                // Wait, we must be careful not to replace `$var2` when replacing `$var`.
                
                // For simplicity, let's just do a naive textual replacement.
                char* current_str = symbols->allocator->reallocate(NULL, strlen(str) + 1, symbols->allocator->user_data);
                strcpy(current_str, str);
                
                for (size_t i = 0; i < symbols->rename_globals_count; i++) {
                    const char* var = symbols->rename_globals[i];
                    bool is_local = active_locals && string_in_list(*active_locals, *active_locals_count, var);
                    if (!is_local && var && strlen(var) > 0) {
                        // var is the variable name WITHOUT sigil (e.g. "my_global")
                        char* pos = current_str;
                        while ((pos = strstr(pos, var)) != NULL) {
                            char next_char = pos[strlen(var)];
                            if ((next_char >= 'a' && next_char <= 'z') || 
                                (next_char >= 'A' && next_char <= 'Z') || 
                                (next_char >= '0' && next_char <= '9') || 
                                next_char == '_') {
                                pos += strlen(var);
                                continue;
                            }
                            
                            // Ensure it's preceded by a sigil ($, @, %)
                            bool is_brace_wrapped = (pos >= current_str + 2 && *(pos - 1) == '{' && (*(pos - 2) == '$' || *(pos - 2) == '@' || *(pos - 2) == '%'));
                            bool is_normal = (pos > current_str && (*(pos - 1) == '$' || *(pos - 1) == '@' || *(pos - 1) == '%'));
                            
                            if (!is_brace_wrapped && !is_normal) {
                                pos += strlen(var);
                                continue;
                            }
                            
                            size_t new_len = strlen(current_str) + strlen(prefix) + 1;
                            char* new_str = symbols->allocator->reallocate(NULL, new_len, symbols->allocator->user_data);
                            size_t prefix_len = pos - current_str;
                            strncpy(new_str, current_str, prefix_len);
                            new_str[prefix_len] = '\0';
                            
                            // Insert prefix, then var
                            size_t curr_idx = prefix_len;
                            strcpy(new_str + curr_idx, prefix);
                            curr_idx += strlen(prefix);
                            strcpy(new_str + curr_idx, var);
                            curr_idx += strlen(var);
                            strcpy(new_str + curr_idx, pos + strlen(var));
                            
                            symbols->allocator->reallocate(current_str, 0, symbols->allocator->user_data);
                            current_str = new_str;
                            pos = current_str + prefix_len + strlen(prefix) + strlen(var);
                        }
                    }
                }
                
                if (strcmp(current_str, str) != 0) {
                    slp_ast_set_string_val(node, current_str, symbols->allocator);
                }
                symbols->allocator->reallocate(current_str, 0, symbols->allocator->user_data);
            }
            break;
        }
        case SLP_AST_SCALAR:
        case SLP_AST_ARRAY:
        case SLP_AST_HASHTABLE: {
            const char* var_name = slp_ast_get_string(node);
            if (var_name && string_in_list(symbols->rename_globals, symbols->rename_globals_count, var_name)) {
                bool is_local = active_locals && string_in_list(*active_locals, *active_locals_count, var_name);
                if (!is_local) {
                    apply_prefix(node, prefix, symbols->allocator);
                }
            }
            break;
        }
        default: {
            SlpASTNode** children;
            size_t count;
            slp_ast_get_children(node, &children, &count, symbols->allocator);
            for (size_t i = 0; i < count; i++) {
                rewrite_pass(children[i], symbols, prefix, rel_path, project_name, active_locals, active_locals_count);
            }
            slp_ast_free_children(children, symbols->allocator);
            break;
        }
    }
}



static void rewrite_script_resources(SlpASTNode* node, const char* file_dir, SlpAllocator* allocator) {
    if (!node) return;
    if (node->is_rewritten) return;
    
    if (node->type == SLP_AST_CALL) {
        if (node->as.call.target && node->as.call.target->type == SLP_AST_IDENTIFIER) {
            const char* func_name = slp_ast_get_string(node->as.call.target);
            if (func_name && strcmp(func_name, "script_resource") == 0 && node->as.call.arg_count > 0 && node->as.call.args[0]) {
                SlpASTNode* arg_node = node->as.call.args[0];
                bool is_arg_wrapped = (arg_node->type == SLP_AST_ARG);
                SlpASTNode* val_node = is_arg_wrapped ? arg_node->as.arg.value : arg_node;
                
                size_t path_len = strlen(file_dir) + 2;
                char* dir_str = allocator->reallocate(NULL, path_len, allocator->user_data);
                strcpy(dir_str, file_dir);
                strcat(dir_str, "/");
                
                SlpASTNode* prefix_node = slp_ast_build_node(SLP_AST_STRING, node->line, allocator);
                slp_ast_set_string_val(prefix_node, dir_str, allocator);
                allocator->reallocate(dir_str, 0, allocator->user_data);
                
                SlpASTNode* binop = slp_ast_build_node(SLP_AST_BINOP, node->line, allocator);
                slp_ast_set_binop_with_op(binop, prefix_node, val_node, ".", allocator);
                
                if (is_arg_wrapped) {
                    arg_node->as.arg.value = binop;
                } else {
                    node->as.call.args[0] = binop;
                }
                node->is_rewritten = true;
            }
        }
    }
    
    SlpASTNode** children;
    size_t count;
    slp_ast_get_children(node, &children, &count, allocator);
    for (size_t i = 0; i < count; i++) {
        rewrite_script_resources(children[i], file_dir, allocator);
    }
    slp_ast_free_children(children, allocator);
}

static void inline_includes(SlpASTNode* node, SymbolTable* symbols, const char* prefix, const char* current_dir, const char* project_name, int depth);

static SlpASTNode* process_file(const char* filepath, const char* prefix, const char* project_dir, const char* project_name, SymbolTable* symbols, int depth, SlpAllocator* allocator) {
    if (depth > 8) {
        fprintf(stderr, "Max include depth exceeded at %s\n", filepath);
        return NULL;
    }
    
    char resolved_path[PATH_MAX];
    const char* path_to_track = filepath;
    if (realpath(filepath, resolved_path) != NULL) {
        path_to_track = resolved_path;
    }
    
    if (string_in_list(symbols->loaded_paths, symbols->loaded_paths_count, path_to_track)) {
        // Skip already loaded files to prevent infinite circular dependency loops
        return NULL;
    }
    
    add_string_to_list(&symbols->loaded_paths, &symbols->loaded_paths_count, path_to_track, allocator);

    char* source = utils_read_file(filepath, NULL);
    if (!source) return NULL;

    SlpParser parser;
    slp_parser_init(&parser, source, allocator);
    SlpASTNode* root = slp_parser_parse(&parser);
    
    symbols->sources = allocator->reallocate(symbols->sources, sizeof(char*) * (symbols->sources_count + 1), allocator->user_data);
    symbols->sources[symbols->sources_count++] = source;
    
    if (parser.had_error) {
        fprintf(stderr, "Failed to parse %s\n", filepath);
        if (parser.error_message) {
            fprintf(stderr, "[line %d] Error: %s\n", parser.error_line, parser.error_message);
        }
        if (root) slp_parser_free_node(root, allocator);
        return NULL;
    }
    
    if (root) {
        char* current_dir = utils_get_directory(filepath);
        
        inline_includes(root, symbols, prefix, current_dir, project_name, depth);
        
        rewrite_script_resources(root, current_dir, allocator);
        
        if (depth == 0) {
            scan_pass(root, symbols, true, project_name);
            
            char** active_locals = NULL;
            size_t active_locals_count = 0;
            rewrite_pass(root, symbols, prefix, project_dir, project_name, &active_locals, &active_locals_count);
        }
        
        free(current_dir);
    }
    
    return root;
}

static void inline_includes(SlpASTNode* node, SymbolTable* symbols, const char* prefix, const char* current_dir, const char* project_name, int depth) {
    if (!node) return;
    
    if (node->type == SLP_AST_CALL) {
        if (node->as.call.target && node->as.call.target->type == SLP_AST_IDENTIFIER) {
            const char* fn = slp_ast_get_string(node->as.call.target);
            if (fn && strcmp(fn, "include") == 0 && node->as.call.arg_count > 0 && node->as.call.args[0]) {
                SlpASTNode* arg0 = node->as.call.args[0];
                if (arg0->type == SLP_AST_ARG) arg0 = arg0->as.arg.value;
                
                const char* inc_path = NULL;
                if (arg0 && (arg0->type == SLP_AST_STRING || arg0->type == SLP_AST_LITERAL)) {
                    inc_path = slp_ast_get_string(arg0);
                } else if (arg0 && arg0->type == SLP_AST_CALL && arg0->as.call.target && arg0->as.call.target->type == SLP_AST_IDENTIFIER) {
                    const char* inner_fn = slp_ast_get_string(arg0->as.call.target);
                    if (inner_fn && strcmp(inner_fn, "script_resource") == 0 && arg0->as.call.arg_count > 0 && arg0->as.call.args[0]) {
                        SlpASTNode* inner_arg0 = arg0->as.call.args[0];
                        if (inner_arg0->type == SLP_AST_ARG) inner_arg0 = inner_arg0->as.arg.value;
                        if (inner_arg0 && (inner_arg0->type == SLP_AST_STRING || inner_arg0->type == SLP_AST_LITERAL)) {
                            inc_path = slp_ast_get_string(inner_arg0);
                        }
                    }
                }
                
                if (inc_path) {
                    size_t path_len = strlen(current_dir) + strlen(inc_path) + 2;
                    char* full_path = symbols->allocator->reallocate(NULL, path_len, symbols->allocator->user_data);
                    strcpy(full_path, current_dir);
                    strcat(full_path, "/");
                    strcat(full_path, inc_path);
                    
                    SlpASTNode* inc_root = process_file(full_path, prefix, current_dir, project_name, symbols, depth + 1, symbols->allocator);
                    symbols->allocator->reallocate(full_path, 0, symbols->allocator->user_data);
                    
                    if (inc_root) {
                        // Free the old CALL node children
                        for (size_t i = 0; i < node->as.call.arg_count; i++) {
                            slp_parser_free_node(node->as.call.args[i], symbols->allocator);
                        }
                        symbols->allocator->reallocate(node->as.call.args, 0, symbols->allocator->user_data);
                        slp_parser_free_node(node->as.call.target, symbols->allocator);
                        
                        // Convert this CALL node to a BLOCK node containing the included file's statements
                        node->type = SLP_AST_BLOCK;
                        
                        if (inc_root->type == SLP_AST_SCRIPT || inc_root->type == SLP_AST_BLOCK) {
                            size_t stmt_count = inc_root->as.block.count;
                            node->as.block.count = stmt_count;
                            node->as.block.capacity = stmt_count;
                            node->as.block.statements = symbols->allocator->reallocate(NULL, sizeof(SlpASTNode*) * stmt_count, symbols->allocator->user_data);
                            
                            for (size_t i = 0; i < stmt_count; i++) {
                                node->as.block.statements[i] = inc_root->as.block.statements[i];
                            }
                            
                            // Free the wrapper script node without freeing its children
                            symbols->allocator->reallocate(inc_root->as.block.statements, 0, symbols->allocator->user_data);
                            symbols->allocator->reallocate(inc_root, 0, symbols->allocator->user_data);
                        } else {
                            node->as.block.count = 1;
                            node->as.block.capacity = 1;
                            node->as.block.statements = symbols->allocator->reallocate(NULL, sizeof(SlpASTNode*), symbols->allocator->user_data);
                            node->as.block.statements[0] = inc_root;
                        }
                        return; // Node transformed, skip further processing on its children since they were already processed in process_file
                    }
                }
            }
        }
    }
    
    SlpASTNode** children;
    size_t count;
    slp_ast_get_children(node, &children, &count, symbols->allocator);
    for (size_t i = 0; i < count; i++) {
        inline_includes(children[i], symbols, prefix, current_dir, project_name, depth);
    }
    slp_ast_free_children(children, symbols->allocator);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: cna_bundler <projects-dir> [-o <output.cna>] [--scan-only]\n");
        return 64; // EX_USAGE
    }
    
    const char* target_dir = argv[1];
    const char* output_file = NULL;
    const char* config_file = NULL;
    bool scan_only = false;
    
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_file = argv[++i];
        } else if (strcmp(argv[i], "--scan-only") == 0) {
            scan_only = true;
        }
    }
    
    RenameConfig config = {0};
    if (config_file) {
        parse_config(config_file, &config);
    }
    
    SlpAllocator allocator;
    allocator.reallocate = default_realloc;
    allocator.user_data = NULL;
    
    SymbolTable symbols;
    init_symbol_table(&symbols, &allocator, &config);
    
    DIR* dir = opendir(target_dir);
    if (!dir) {
        struct stat st;
        if (stat(target_dir, &st) == 0 && S_ISREG(st.st_mode)) {
            // Processing a single file instead of directory
            char* project_dir = strdup(target_dir);
            char* last_slash = strrchr(project_dir, '/');
            char* last_backslash = strrchr(project_dir, '\\');
            if (last_backslash > last_slash) last_slash = last_backslash;
            if (last_slash) *last_slash = '\0'; else strcpy(project_dir, ".");
            
            SlpASTNode* root = process_file(target_dir, "Proj_", project_dir, "Proj", &symbols, 0, &allocator);
            if (root) {
                if (scan_only) print_symbols(target_dir, &symbols);
                else {
                    char* formatted = slp_ast_format(root, &allocator);
                    if (formatted) {
                        if (output_file) {
                            FILE* out = fopen(output_file, "w");
                            if (out) { fputs(formatted, out); fclose(out); }
                        } else {
                            printf("%s\n", formatted);
                        }
                        allocator.reallocate(formatted, 0, allocator.user_data);
                    }
                }
                slp_parser_free_node(root, &allocator);
            }
            free(project_dir);
            free_symbol_table(&symbols);
            return root ? 0 : 1;
        }
        fprintf(stderr, "Could not open directory %s\n", target_dir);
        return 1;
    }
    
    // Bundle mode
    SlpASTNode* bundle_root = slp_ast_build_node(SLP_AST_BLOCK, 1, &allocator);
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] != '.') {
            char path[1024];
            snprintf(path, sizeof(path), "%s/%s", target_dir, entry->d_name);
            
            struct stat st;
            if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
                char* entrypoint = find_entrypoint(path, entry->d_name);
            if (entrypoint) {
                // Determine a safe prefix (sanitize directory name)
                char prefix[256];
                snprintf(prefix, sizeof(prefix), "%s_", entry->d_name);
                for (int i = 0; prefix[i]; i++) {
                    if (prefix[i] == '-' || prefix[i] == ' ') prefix[i] = '_';
                }
                
                fprintf(stderr, "Bundling project %s from %s with prefix %s\n", entry->d_name, entrypoint, prefix);
                SlpASTNode* proj_root = process_file(entrypoint, prefix, path, entry->d_name, &symbols, 0, &allocator);
                if (proj_root) {
                    if (scan_only) {
                        print_symbols(entrypoint, &symbols);
                    } else if (proj_root->type == SLP_AST_SCRIPT || proj_root->type == SLP_AST_BLOCK) {
                        size_t stmt_count = proj_root->as.block.count;
                        size_t old_count = bundle_root->as.block.count;
                        size_t new_count = old_count + stmt_count;
                        
                        bundle_root->as.block.capacity = new_count;
                        bundle_root->as.block.statements = allocator.reallocate(bundle_root->as.block.statements, sizeof(SlpASTNode*) * new_count, allocator.user_data);
                        
                        for (size_t i = 0; i < stmt_count; i++) {
                            bundle_root->as.block.statements[old_count + i] = proj_root->as.block.statements[i];
                        }
                        bundle_root->as.block.count = new_count;
                        
                        allocator.reallocate(proj_root->as.block.statements, 0, allocator.user_data);
                        allocator.reallocate(proj_root, 0, allocator.user_data);
                    }
                }
                free(entrypoint);
            }
            }
        }
    }
    closedir(dir);
    
    // Check for collisions across all aliases before proceeding to generation
    if (!scan_only) {
        bool has_collisions = false;
        for (size_t i = 0; i < symbols.alias_targets_count; i++) {
            const char* base_alias = symbols.alias_targets[i].name;
            const char* base_project = symbols.alias_targets[i].project;
            const char* final_base = get_renamed_alias(&symbols, base_project, base_alias);
            
            for (size_t j = i + 1; j < symbols.alias_targets_count; j++) {
                const char* other_alias = symbols.alias_targets[j].name;
                const char* other_project = symbols.alias_targets[j].project;
                const char* final_other = get_renamed_alias(&symbols, other_project, other_alias);
                
                if (strcmp(final_base, final_other) == 0) {
                    fprintf(stderr, "[!] ERROR: Unresolved alias collision detected!\n");
                    fprintf(stderr, "    Alias '%s' in project '%s' and alias '%s' in project '%s' both resolve to '%s'.\n", 
                            base_alias, base_project, other_alias, other_project, final_base);
                    fprintf(stderr, "    Please add a rename rule to your config file to resolve this conflict.\n");
                    has_collisions = true;
                }
            }
        }
        
        if (has_collisions) {
            slp_parser_free_node(bundle_root, &allocator);
            free_symbol_table(&symbols);
            if (config.rules) free(config.rules);
            return 1;
        }
    }
    
    if (!scan_only) {
        char* formatted = slp_ast_format(bundle_root, &allocator);
        if (formatted) {
            if (output_file) {
                FILE* out = fopen(output_file, "w");
                if (out) {
                    fputs("# Auto-generated bundle by cna_bundler\n", out);
                    fputs(formatted, out);
                    fclose(out);
                    fprintf(stderr, "Wrote bundled CNA to %s\n", output_file);
                } else {
                    fprintf(stderr, "Could not open output file %s\n", output_file);
                }
            } else {
                printf("%s\n", formatted);
            }
            allocator.reallocate(formatted, 0, allocator.user_data);
        }
    }
    
    slp_parser_free_node(bundle_root, &allocator);
    free_symbol_table(&symbols);
    if (config.rules) free(config.rules);
    
    return 0;
}