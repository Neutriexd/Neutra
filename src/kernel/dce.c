#include "dce.h"
#include "vga.h"
#include "kernel.h"

#define MAKE_COLOR(bg, fg) ((bg << 4) | fg)
#define COLOR_BLACK 0
#define COLOR_WHITE 15

void dce_vm_init(DCE_VM* vm) {
    memset(vm, 0, sizeof(DCE_VM));
    vm->sp = 0;
    vm->pc = 0;
    vm->running = 1;
}

void dce_vm_load_cde(DCE_VM* vm, const uint8_t* program, uint32_t size)
{
    if (size < sizeof(CDE_Header)) {
        vm->running = 0;
        return;
    }

    const CDE_Header* header = (const CDE_Header*)program;

    if (header->magic != CDE_MAGIC ||
        header->version != CDE_VERSION) {
        vm->running = 0;
        return;
    }

    uint32_t total_size = header->code_size + header->data_size;

    if (total_size > MAX_DCE_CODE) {
        vm->running = 0;
        return;
    }

    if (header->code_offset + header->code_size > size) {
        vm->running = 0;
        return;
    }

    if (header->data_offset + header->data_size > size) {
        vm->running = 0;
        return;
    }

    vm->pc = header->entry_point;
    vm->code_end = header->code_size;
    vm->data_start = header->code_size;

    for (uint32_t i = 0; i < header->code_size; i++) {
        vm->code[i] =
            program[header->code_offset + i];
    }

    for (uint32_t i = 0; i < header->data_size; i++) {
        vm->code[header->code_size + i] =
            program[header->data_offset + i];
    }

    vm->running = 1;
}

static void dce_push(DCE_VM* vm, int32_t val) {
    if (vm->sp < 256) {
        vm->stack[vm->sp++] = val;
    }
}

static int32_t dce_pop(DCE_VM* vm) {
    if (vm->sp > 0) {
        return vm->stack[--vm->sp];
    }
    return 0;
}

void dce_vm_step(DCE_VM* vm) {
    if (!vm->running) {
        return;
    }

    if (vm->pc >= vm->code_end) {
        vm->running = 0;
        return;
    }

    uint8_t op = vm->code[vm->pc++];

    
    switch (op) {
        case DCE_NOP:
            break;
        case DCE_PUSH: {
            int32_t val = *(int32_t*)&vm->code[vm->pc];
            vm->pc += 4;
            dce_push(vm, val);
            break;
        }
        case DCE_POP: {
            dce_pop(vm);
            break;
        }
        case DCE_ADD: {
            int32_t b = dce_pop(vm);
            int32_t a = dce_pop(vm);
            dce_push(vm, a + b);
            break;
        }
        case DCE_SUB: {
            int32_t b = dce_pop(vm);
            int32_t a = dce_pop(vm);
            dce_push(vm, a - b);
            break;
        }
        case DCE_MUL: {
            int32_t b = dce_pop(vm);
            int32_t a = dce_pop(vm);
            dce_push(vm, a * b);
            break;
        }
        case DCE_DIV: {
            int32_t b = dce_pop(vm);
            int32_t a = dce_pop(vm);
            if (b != 0) {
                dce_push(vm, a / b);
            } else {
                dce_push(vm, 0);
            }
            break;
        }
        case DCE_MOD: {
            int32_t b = dce_pop(vm);
            int32_t a = dce_pop(vm);
            if (b != 0) {
                dce_push(vm, a % b);
            } else {
                dce_push(vm, 0);
            }
            break;
        }
        
        case DCE_PRINT_CHAR: {
            int32_t val = dce_pop(vm);
            vga_putchar((char)val, MAKE_COLOR(COLOR_BLACK, COLOR_WHITE));
            break;
        }
        case DCE_PRINT_STR: {
            int32_t string_id = dce_pop(vm);  
            
            
            int search_pos = MAX_DCE_CODE - 1;
            while (search_pos > 0 && vm->code[search_pos] != 0xFF) {
                search_pos--;
            }
            
            if (search_pos > 0 && vm->code[search_pos] == 0xFF) {
                search_pos++;
                int32_t num_strings = *(int32_t*)&vm->code[search_pos];
                search_pos += 4;
                
                int current_id = 0;
                while (current_id < num_strings && search_pos < MAX_DCE_CODE) {
                    int32_t str_len = *(int32_t*)&vm->code[search_pos];
                    search_pos += 4;
                    
                    if (current_id == string_id) {
                        for (int i = 0; i < str_len && search_pos < MAX_DCE_CODE; i++) {
                            vga_putchar((char)vm->code[search_pos], MAKE_COLOR(COLOR_BLACK, COLOR_WHITE));
                            search_pos++;
                        }
                        break;
                    }
                    
                    search_pos += str_len;
                    current_id++;
                }
            }
            break;
        }
        case DCE_SET_VAR: {
            uint8_t var_id = vm->code[vm->pc++];
            if (var_id < MAX_DCE_VARS) {
                vm->vars[var_id] = dce_pop(vm);
            }
            break;
        }
        case DCE_GET_VAR: {
            uint8_t var_id = vm->code[vm->pc++];
            if (var_id < MAX_DCE_VARS) {
                dce_push(vm, vm->vars[var_id]);
            }
            break;
        }
        case DCE_JMP: {
            int32_t addr = *(int32_t*)&vm->code[vm->pc];
            vm->pc = addr;
            break;
        }
        case DCE_JZ: {
            int32_t addr = *(int32_t*)&vm->code[vm->pc];
            vm->pc += 4;
            if (dce_pop(vm) == 0) {
                vm->pc = addr;
            }
            break;
        }
        case DCE_JNZ: {
            int32_t addr = *(int32_t*)&vm->code[vm->pc];
            vm->pc += 4;
            if (dce_pop(vm) != 0) {
                vm->pc = addr;
            }
            break;
        }
        case DCE_HALT:
            vm->running = 0;
            break;
        default:
            vm->running = 0;
            break;
    }
}

void dce_vm_execute(DCE_VM* vm) {
    while (vm->running) {
        dce_vm_step(vm);
    }
}