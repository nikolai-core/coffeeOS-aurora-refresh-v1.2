/* multiboot entry for higher-half i386 kernel */

.set ALIGN,    1<<0
.set MEMINFO,  1<<1
.set VIDEO,    1<<2
.set FLAGS,    ALIGN | MEMINFO | VIDEO
.set MAGIC,    0x1BADB002
.set CHECKSUM, -(MAGIC + FLAGS)

.set PAGE_SIZE, 4096
.set IDENTITY_TABLES, 16              /* 16 * 4 MiB = 64 MiB identity */
.set KERNEL_PD_INDEX, 768             /* 0xC0000000 >> 22 */

.section .multiboot,"a"
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM
.long 0
.long 0
.long 0
.long 0
.long 0
.long 0
.long 1024
.long 768
.long 32

.section .bss.boot,"aw",@nobits
.align 16
boot_mb_magic:
    .skip 4
boot_mb_info:
    .skip 4

stack_bottom:
    .skip 16384
stack_top:

.align 4096
boot_page_directory:
    .skip 4096

.align 4096
boot_page_tables:
    .skip (IDENTITY_TABLES * 4096)

.section .text.boot,"ax"
.code32

.global _start
.extern kmain

_start:
    cli
    mov $stack_top, %esp
    mov $stack_top, %ebp

    mov %eax, boot_mb_magic
    mov %ebx, boot_mb_info

    mov $boot_page_directory, %edi
    xor %eax, %eax
    mov $(4096 / 4), %ecx
    rep stosl

    xor %esi, %esi                    /* table index i */
.Lpt_loop:
    cmp $IDENTITY_TABLES, %esi
    je .Lpt_done

    mov $boot_page_tables, %edi
    mov %esi, %eax
    shl $12, %eax
    add %eax, %edi

    mov %esi, %eax
    shl $22, %eax                     /* i * 4 MiB */
    or $0x3, %eax

    mov $1024, %ecx
.Lpte_loop:
    mov %eax, (%edi)
    add $4, %edi
    add $PAGE_SIZE, %eax
    loop .Lpte_loop

    mov $boot_page_directory, %edi
    mov $boot_page_tables, %eax
    mov %esi, %ebx
    shl $12, %ebx
    add %ebx, %eax
    or $0x3, %eax

    mov %esi, %ebx
    shl $2, %ebx
    mov %eax, (%edi,%ebx,1)

    mov $(KERNEL_PD_INDEX * 4), %ebx
    mov %esi, %edx
    shl $2, %edx
    add %edx, %ebx
    mov %eax, (%edi,%ebx,1)

    inc %esi
    jmp .Lpt_loop

.Lpt_done:
    /* recursive slot */
    mov $boot_page_directory, %edi
    mov $boot_page_directory, %eax
    or $0x3, %eax
    mov %eax, (1023 * 4)(%edi)

    mov $boot_page_directory, %eax
    mov %eax, %cr3

    mov %cr0, %eax
    or $(1<<16), %eax                 /* WP */
    or $(1<<31), %eax                 /* PG */
    mov %eax, %cr0

    /* switch stacks before later CR3 changes */
    mov $stack_top, %eax
    add $0xC0000000, %eax
    mov %eax, %esp
    mov %eax, %ebp

    mov boot_mb_info, %ebx
    mov boot_mb_magic, %eax
    push %ebx
    push %eax
    mov $kmain, %eax
    call *%eax

.Lhang:
    hlt
    jmp .Lhang

.section .note.GNU-stack,"",@progbits
