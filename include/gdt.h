#ifndef GDT_H
#define GDT_H

#include <stdint.h>

#define GDT_KERNEL_CODE_SEL 0x08u
#define GDT_KERNEL_DATA_SEL 0x10u
#define GDT_USER_CODE_SEL 0x1Bu
#define GDT_USER_DATA_SEL 0x23u
#define GDT_TSS_SEL 0x28u

void gdt_init(uint32_t kernel_stack_top);
void tss_set_kernel_stack(uint32_t kernel_stack_top);

#endif
