.section .text
.code32

.global _start
.extern main

_start:
    call main
    mov %eax, %ebx
    mov $2, %eax
    int $0x80

hang:
    hlt
    jmp hang

.section .note.GNU-stack,"",@progbits
