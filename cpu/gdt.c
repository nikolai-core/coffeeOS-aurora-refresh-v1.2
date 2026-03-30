#include <stdint.h>

#include "gdt.h"

struct __attribute__((packed)) GDTEntry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
};

struct __attribute__((packed)) GDTPointer {
    uint16_t limit;
    uint32_t base;
};

struct __attribute__((packed)) TSS {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
};

static struct GDTEntry gdt[6];
static struct GDTPointer gdt_ptr;
static struct TSS tss;

static void memzero(void *ptr, uint32_t len) {
    uint8_t *p = (uint8_t *)ptr;
    uint32_t i;
    for (i = 0; i < len; i++) {
        p[i] = 0;
    }
}

static inline void gdt_set_entry(
    int index,
    uint32_t base,
    uint32_t limit,
    uint8_t access,
    uint8_t granularity
) {
    gdt[index].base_low = (uint16_t)(base & 0xFFFFu);
    gdt[index].base_mid = (uint8_t)((base >> 16) & 0xFFu);
    gdt[index].base_high = (uint8_t)((base >> 24) & 0xFFu);

    gdt[index].limit_low = (uint16_t)(limit & 0xFFFFu);
    gdt[index].granularity = (uint8_t)((limit >> 16) & 0x0Fu);
    gdt[index].granularity |= granularity & 0xF0u;

    gdt[index].access = access;
}

void tss_set_kernel_stack(uint32_t kernel_stack_top) {
    tss.esp0 = kernel_stack_top;
}

void gdt_init(uint32_t kernel_stack_top) {
    gdt_ptr.limit = sizeof(gdt) - 1u;
    gdt_ptr.base = (uint32_t)&gdt;

    gdt_set_entry(0, 0, 0, 0, 0);
    gdt_set_entry(1, 0, 0xFFFFFFFFu, 0x9Au, 0xCFu);
    gdt_set_entry(2, 0, 0xFFFFFFFFu, 0x92u, 0xCFu);
    gdt_set_entry(3, 0, 0xFFFFFFFFu, 0xFAu, 0xCFu);
    gdt_set_entry(4, 0, 0xFFFFFFFFu, 0xF2u, 0xCFu);

    memzero(&tss, sizeof(tss));
    tss.ss0 = GDT_KERNEL_DATA_SEL;
    tss.esp0 = kernel_stack_top;
    tss.cs = GDT_USER_CODE_SEL;
    tss.ss = GDT_USER_DATA_SEL;
    tss.ds = GDT_USER_DATA_SEL;
    tss.es = GDT_USER_DATA_SEL;
    tss.fs = GDT_USER_DATA_SEL;
    tss.gs = GDT_USER_DATA_SEL;
    tss.iomap_base = (uint16_t)sizeof(tss);

    gdt_set_entry(5, (uint32_t)&tss, sizeof(tss) - 1u, 0x89u, 0x40u);

    __asm__ volatile ("lgdt %0" : : "m"(gdt_ptr));

    __asm__ volatile (
        "ljmp $0x08, $.reload_cs\n"
        ".reload_cs:\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        :
        :
        : "ax", "memory"
    );

    __asm__ volatile ("ltr %%ax" : : "a"((uint16_t)GDT_TSS_SEL));
}
