#include "coff.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif
#endif

#pragma pack(push, 1)
typedef struct {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
} COFF_FILE_HEADER;

typedef struct {
    char Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
} COFF_SECTION_HEADER;

typedef struct {
    uint32_t VirtualAddress;
    uint32_t SymbolTableIndex;
    uint16_t Type;
} COFF_RELOCATION;

typedef struct {
    union {
        char ShortName[8];
        struct {
            uint32_t Zeroes;
            uint32_t Offset;
        } Name;
    } N;
    uint32_t Value;
    int16_t SectionNumber;
    uint16_t Type;
    uint8_t StorageClass;
    uint8_t NumberOfAuxSymbols;
} COFF_SYMBOL;

typedef struct {
    uint8_t jmp_opcode[6]; // FF 25 00 00 00 00
    uint64_t address;
} tramp_x64_t;
#pragma pack(pop)

#define IMAGE_REL_AMD64_ADDR64 0x0001
#define IMAGE_REL_AMD64_ADDR32 0x0002
#define IMAGE_REL_AMD64_ADDR32NB 0x0003
#define IMAGE_REL_AMD64_REL32 0x0004
#define IMAGE_REL_AMD64_REL32_1 0x0005
#define IMAGE_REL_AMD64_REL32_2 0x0006
#define IMAGE_REL_AMD64_REL32_3 0x0007
#define IMAGE_REL_AMD64_REL32_4 0x0008
#define IMAGE_REL_AMD64_REL32_5 0x0009

#define IMAGE_REL_I386_DIR32 0x0006
#define IMAGE_REL_I386_DIR32NB 0x0007
#define IMAGE_REL_I386_REL32 0x0014

static size_t align_up(size_t val, size_t alignment) {
    return (val + alignment - 1) & ~(alignment - 1);
}

int coff_load(coff_module_t *module, const uint8_t *data, size_t data_len, const char *entrypoint_name, coff_symbol_resolver_t resolver, void *userdata) {
    if (!module || !data || data_len < sizeof(COFF_FILE_HEADER)) return -1;
    
    memset(module, 0, sizeof(*module));
    
    COFF_FILE_HEADER *header = (COFF_FILE_HEADER *)data;
    if (header->Machine != 0x8664 && header->Machine != 0x014c) return -2;
    
    COFF_SECTION_HEADER *sections = (COFF_SECTION_HEADER *)(data + sizeof(COFF_FILE_HEADER));
    COFF_SYMBOL *symbols = (COFF_SYMBOL *)(data + header->PointerToSymbolTable);
    char *strings = (char *)(symbols + header->NumberOfSymbols);
    
    size_t total_memory_needed = 0;
    for (int i = 0; i < header->NumberOfSections; i++) {
        size_t s_size = sections[i].SizeOfRawData > 0 ? sections[i].SizeOfRawData : sections[i].VirtualSize;
        total_memory_needed += align_up(s_size, 16);
    }
    
    size_t tramp_size = header->NumberOfSymbols * sizeof(tramp_x64_t);
    total_memory_needed += align_up(tramp_size, 16);
    
    if (total_memory_needed == 0) return -1;
    
    uint8_t *mem = NULL;
#ifdef _WIN32
    mem = VirtualAlloc(NULL, total_memory_needed, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
#else
    mem = mmap(NULL, total_memory_needed, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) mem = NULL;
#endif

    if (!mem) return -3;
    
    module->allocated_memory = mem;
    module->allocated_size = total_memory_needed;
    
    void **section_mappings = calloc(header->NumberOfSections, sizeof(void*));
    uint8_t *mem_ptr = mem;
    
    for (int i = 0; i < header->NumberOfSections; i++) {
        size_t s_size = sections[i].SizeOfRawData > 0 ? sections[i].SizeOfRawData : sections[i].VirtualSize;
        if (s_size > 0) {
            section_mappings[i] = mem_ptr;
            if (sections[i].SizeOfRawData > 0) {
                memcpy(mem_ptr, data + sections[i].PointerToRawData, sections[i].SizeOfRawData);
            } else {
                memset(mem_ptr, 0, s_size);
            }
            mem_ptr += align_up(s_size, 16);
        }
    }
    
    tramp_x64_t *trampolines = (tramp_x64_t *)mem_ptr;
    int tramp_count = 0;
    
    for (int i = 0; i < header->NumberOfSections; i++) {
        if (!section_mappings[i] || sections[i].NumberOfRelocations == 0) continue;
        COFF_RELOCATION *rels = (COFF_RELOCATION *)(data + sections[i].PointerToRelocations);
        
        for (int j = 0; j < sections[i].NumberOfRelocations; j++) {
            COFF_SYMBOL *sym = &symbols[rels[j].SymbolTableIndex];
            void *sym_addr = NULL;
            
            if (sym->SectionNumber > 0) {
                if (sym->SectionNumber <= header->NumberOfSections) {
                    sym_addr = (uint8_t *)section_mappings[sym->SectionNumber - 1] + sym->Value;
                }
            } else {
                char *sym_name = (sym->N.Name.Zeroes == 0) ? (strings + sym->N.Name.Offset) : (char *)sym->N.ShortName;
                char clean_name[256] = {0};
                if (sym->N.Name.Zeroes != 0) {
                    strncpy(clean_name, (char*)sym->N.ShortName, 8);
                } else {
                    strncpy(clean_name, sym_name, 255);
                }
                
                void *resolved = resolver ? resolver(clean_name, userdata) : NULL;
                if (resolved) {
                    tramp_x64_t *tramp = &trampolines[tramp_count++];
                    tramp->jmp_opcode[0] = 0xFF;
                    tramp->jmp_opcode[1] = 0x25;
                    tramp->jmp_opcode[2] = 0x00;
                    tramp->jmp_opcode[3] = 0x00;
                    tramp->jmp_opcode[4] = 0x00;
                    tramp->jmp_opcode[5] = 0x00;
                    tramp->address = (uint64_t)(uintptr_t)resolved;
                    
                    if (strncmp(clean_name, "__imp_", 6) == 0) {
                        sym_addr = &tramp->address;
                    } else {
                        sym_addr = tramp;
                    }
                }
            }
            
            if (!sym_addr) continue;
            
            uint8_t *target = (uint8_t *)section_mappings[i] + rels[j].VirtualAddress;
            uint32_t offsetvalue32 = 0;
            uint64_t offsetvalue64 = 0;
            
            if (header->Machine == 0x8664) {
                if (rels[j].Type == IMAGE_REL_AMD64_ADDR64) {
                    memcpy(&offsetvalue64, target, sizeof(uint64_t));
                    offsetvalue64 += (uint64_t)(uintptr_t)sym_addr;
                    memcpy(target, &offsetvalue64, sizeof(uint64_t));
                } else if (rels[j].Type == IMAGE_REL_AMD64_ADDR32NB) {
                    memcpy(&offsetvalue32, target, sizeof(uint32_t));
                    offsetvalue32 += (uint32_t)((uintptr_t)sym_addr - (uintptr_t)(target + 4));
                    memcpy(target, &offsetvalue32, sizeof(uint32_t));
                } else if (rels[j].Type == IMAGE_REL_AMD64_REL32) {
                    memcpy(&offsetvalue32, target, sizeof(uint32_t));
                    offsetvalue32 += (uint32_t)((uintptr_t)sym_addr - (uintptr_t)target - 4);
                    memcpy(target, &offsetvalue32, sizeof(uint32_t));
                } else if (rels[j].Type == IMAGE_REL_AMD64_REL32_1) {
                    memcpy(&offsetvalue32, target, sizeof(uint32_t));
                    offsetvalue32 += (uint32_t)((uintptr_t)sym_addr - (uintptr_t)target - 5);
                    memcpy(target, &offsetvalue32, sizeof(uint32_t));
                } else if (rels[j].Type == IMAGE_REL_AMD64_REL32_2) {
                    memcpy(&offsetvalue32, target, sizeof(uint32_t));
                    offsetvalue32 += (uint32_t)((uintptr_t)sym_addr - (uintptr_t)target - 6);
                    memcpy(target, &offsetvalue32, sizeof(uint32_t));
                } else if (rels[j].Type == IMAGE_REL_AMD64_REL32_3) {
                    memcpy(&offsetvalue32, target, sizeof(uint32_t));
                    offsetvalue32 += (uint32_t)((uintptr_t)sym_addr - (uintptr_t)target - 7);
                    memcpy(target, &offsetvalue32, sizeof(uint32_t));
                } else if (rels[j].Type == IMAGE_REL_AMD64_REL32_4) {
                    memcpy(&offsetvalue32, target, sizeof(uint32_t));
                    offsetvalue32 += (uint32_t)((uintptr_t)sym_addr - (uintptr_t)target - 8);
                    memcpy(target, &offsetvalue32, sizeof(uint32_t));
                } else if (rels[j].Type == IMAGE_REL_AMD64_REL32_5) {
                    memcpy(&offsetvalue32, target, sizeof(uint32_t));
                    offsetvalue32 += (uint32_t)((uintptr_t)sym_addr - (uintptr_t)target - 9);
                    memcpy(target, &offsetvalue32, sizeof(uint32_t));
                }
            } else if (header->Machine == 0x014c) {
                if (rels[j].Type == IMAGE_REL_I386_DIR32) {
                    memcpy(&offsetvalue32, target, sizeof(uint32_t));
                    offsetvalue32 += (uint32_t)(uintptr_t)sym_addr;
                    memcpy(target, &offsetvalue32, sizeof(uint32_t));
                } else if (rels[j].Type == IMAGE_REL_I386_REL32) {
                    memcpy(&offsetvalue32, target, sizeof(uint32_t));
                    offsetvalue32 += (uint32_t)((uintptr_t)sym_addr - (uintptr_t)target - 4);
                    memcpy(target, &offsetvalue32, sizeof(uint32_t));
                }
            }
        }
    }
    
    for (uint32_t i = 0; i < header->NumberOfSymbols; i += 1 + symbols[i].NumberOfAuxSymbols) {
        char *sym_name = (symbols[i].N.Name.Zeroes == 0) ? (strings + symbols[i].N.Name.Offset) : (char *)symbols[i].N.ShortName;
        char clean_sym_name[9] = {0};
        if (symbols[i].N.Name.Zeroes != 0) {
            strncpy(clean_sym_name, (char*)symbols[i].N.ShortName, 8);
            sym_name = clean_sym_name;
        }
        if (strcmp(sym_name, entrypoint_name) == 0 && symbols[i].SectionNumber > 0 && symbols[i].SectionNumber <= header->NumberOfSections) {
            module->entrypoint = (uint8_t *)section_mappings[symbols[i].SectionNumber - 1] + symbols[i].Value;
            break;
        }
    }
    
    free(section_mappings);
    
    if (!module->entrypoint) {
        coff_free(module);
        return -4;
    }
    return 0;
}

void coff_execute(coff_module_t *module, const uint8_t *args, size_t args_size) {
    if (!module || !module->entrypoint) return;
#ifdef _WIN32
    void (*go)(const char *, unsigned long) = (void (*)(const char *, unsigned long))module->entrypoint;
    go((const char *)args, (unsigned long)args_size);
#endif
}

void coff_free(coff_module_t *module) {
    if (!module || !module->allocated_memory) return;
#ifdef _WIN32
    VirtualFree(module->allocated_memory, 0, MEM_RELEASE);
#else
    munmap(module->allocated_memory, module->allocated_size);
#endif
    module->allocated_memory = NULL;
    module->allocated_size = 0;
    module->entrypoint = NULL;
}
