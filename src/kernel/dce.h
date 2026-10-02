#ifndef DCE_H
#define DCE_H
#include <stdint.h>

#define MAX_DCE_VARS 256
#define MAX_DCE_CODE 4096
#define CDE_MAGIC 0x43444501
#define CDE_VERSION 1

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t flags;
    uint16_t header_size;
    uint32_t code_offset;
    uint32_t code_size;
    uint32_t data_offset;
    uint32_t data_size;
    uint32_t entry_point;
    uint32_t code_end;
} CDE_Header;

typedef enum {
    DCE_NOP=0x00, DCE_PUSH=0x01, DCE_POP=0x02,
    DCE_ADD=0x03, DCE_SUB=0x04, DCE_MUL=0x05, DCE_DIV=0x06, DCE_MOD=0x07,
    DCE_PRINT=0x08, DCE_PRINT_CHAR=0x09,
    DCE_SET_VAR=0x0A, DCE_GET_VAR=0x0B,
    DCE_JMP=0x0C, DCE_JZ=0x0D, DCE_JNZ=0x0E,
    DCE_CALL=0x0F, DCE_RET=0x10,
    DCE_PRINT_STR=0x15,
    DCE_HALT=0xFF
} DCE_OpCode;

typedef struct {
    int32_t stack[256];
    int32_t sp;

    int32_t vars[MAX_DCE_VARS];

    uint8_t code[MAX_DCE_CODE];

    int32_t pc;
    int32_t running;

    uint32_t code_end;
    uint32_t data_start;
} DCE_VM;
void dce_vm_init(DCE_VM* vm);
void dce_vm_load_cde(DCE_VM* vm, const uint8_t* program, uint32_t size);
void dce_vm_execute(DCE_VM* vm);
void dce_vm_step(DCE_VM* vm);

#endif