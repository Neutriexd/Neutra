extern kernel_entry

global start
section .text
bits 32

start:
    mov esp, stack_top
    push ebx        
    call kernel_entry
    hlt
    jmp start

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:
