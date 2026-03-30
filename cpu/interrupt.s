/* IDT load helper + ISR/IRQ stubs */

.section .text
.code32

.global idt_load
idt_load:
    mov 4(%esp), %eax
    lidt (%eax)
    ret

.extern isr_dispatch

.global userland_enter
userland_enter:
    mov 12(%esp), %eax /* resume context */
    mov %esp, (%eax)
    mov %ebp, 4(%eax)
    mov %ebx, 8(%eax)
    mov %esi, 12(%eax)
    mov %edi, 16(%eax)

    cli

    mov 4(%esp), %ecx /* entry */
    mov 8(%esp), %edx /* user stack top */

    mov $0x23, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    pushl $0x23
    pushl %edx
    pushf
    popl %eax
    orl $0x200, %eax
    pushl %eax
    pushl $0x1B
    pushl %ecx
    iret

.global userland_resume
userland_resume:
    mov 8(%esp), %edx /* resume context */
    mov 4(%esp), %eax /* exit code / return value */
    mov 4(%edx), %ebp
    mov 8(%edx), %ebx
    mov 12(%edx), %esi
    mov 16(%edx), %edi
    mov (%edx), %esp
    ret

.macro ISR_NOERR n
.global isr\n
isr\n:
    pushl $0
    pushl $\n
    jmp isr_common_stub
.endm

.macro ISR_ERR n
.global isr\n
isr\n:
    pushl $\n
    jmp isr_common_stub
.endm

.macro IRQ n, vec
.global irq\n
irq\n:
    pushl $0
    pushl $\vec
    jmp isr_common_stub
.endm

isr_common_stub:
    pusha

    xorl %eax, %eax
    mov %ds, %ax
    pushl %eax
    mov %es, %ax
    pushl %eax
    mov %fs, %ax
    pushl %eax
    mov %gs, %ax
    pushl %eax

    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    cld
    pushl %esp
    call isr_dispatch
    add $4, %esp

    popl %eax
    mov %ax, %gs
    popl %eax
    mov %ax, %fs
    popl %eax
    mov %ax, %es
    popl %eax
    mov %ax, %ds

    popa
    add $8, %esp
    iret

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30
ISR_NOERR 31

IRQ 0, 32
IRQ 1, 33
IRQ 2, 34
IRQ 3, 35
IRQ 4, 36
IRQ 5, 37
IRQ 6, 38
IRQ 7, 39
IRQ 8, 40
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

ISR_NOERR 128

.section .note.GNU-stack,"",@progbits
