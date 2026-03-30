#include <stdint.h>

#include "audio.h"
#include "gfx.h"
#include "io.h"
#include "keyboard.h"
#include "mouse.h"
#include "pit.h"
#include "serial.h"
#include "sb16.h"
#include "syscall_numbers.h"
#include "synth.h"
#include "userland.h"
#include "vmm.h"
#include "vm.h"

struct __attribute__((packed)) IDTEntry {
    uint16_t base_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t flags;
    uint16_t base_high;
};

struct __attribute__((packed)) IDTPointer {
    uint16_t limit;
    uint32_t base;
};

struct InterruptFrame {
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;

    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;

    uint32_t int_no;
    uint32_t err_code;

    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t useresp;
    uint32_t ss;
};

extern void idt_load(uint32_t idt_ptr);
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

extern void isr128(void);

static struct IDTEntry idt[256];
static struct IDTPointer idt_ptr;

#define IDT_FLAG_INTERRUPT_GATE 0x8E
#define IDT_FLAG_USER_INTERRUPT_GATE 0xEE

#define PIC1_CMD 0x20
#define PIC1_DATA 0x21
#define PIC2_CMD 0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI 0x20

static inline uint32_t read_cr2(void) {
    uint32_t value;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(value));
    return value;
}

static void idt_set_gate(uint8_t index, uint32_t base, uint16_t selector, uint8_t flags) {
    idt[index].base_low = (uint16_t)(base & 0xFFFFu);
    idt[index].selector = selector;
    idt[index].zero = 0;
    idt[index].flags = flags;
    idt[index].base_high = (uint16_t)((base >> 16) & 0xFFFFu);
}

static void pic_remap(uint8_t offset1, uint8_t offset2) {
    uint8_t mask1 = io_in8(PIC1_DATA);
    uint8_t mask2 = io_in8(PIC2_DATA);

    io_out8(PIC1_CMD, 0x11);
    io_wait();
    io_out8(PIC2_CMD, 0x11);
    io_wait();

    io_out8(PIC1_DATA, offset1);
    io_wait();
    io_out8(PIC2_DATA, offset2);
    io_wait();

    io_out8(PIC1_DATA, 0x04);
    io_wait();
    io_out8(PIC2_DATA, 0x02);
    io_wait();

    io_out8(PIC1_DATA, 0x01);
    io_wait();
    io_out8(PIC2_DATA, 0x01);
    io_wait();

    io_out8(PIC1_DATA, mask1);
    io_out8(PIC2_DATA, mask2);
}

void idt_init(void) {
    uint32_t i;

    for (i = 0; i < 256u; i++) {
        idt_set_gate((uint8_t)i, 0, 0, 0);
    }

    idt_ptr.limit = (uint16_t)(sizeof(idt) - 1u);
    idt_ptr.base = (uint32_t)&idt;

    idt_set_gate(0, (uint32_t)isr0, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(1, (uint32_t)isr1, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(2, (uint32_t)isr2, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(3, (uint32_t)isr3, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(4, (uint32_t)isr4, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(5, (uint32_t)isr5, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(6, (uint32_t)isr6, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(7, (uint32_t)isr7, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(8, (uint32_t)isr8, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(9, (uint32_t)isr9, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(10, (uint32_t)isr10, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(11, (uint32_t)isr11, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(12, (uint32_t)isr12, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(13, (uint32_t)isr13, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(14, (uint32_t)isr14, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(15, (uint32_t)isr15, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(16, (uint32_t)isr16, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(17, (uint32_t)isr17, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(18, (uint32_t)isr18, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(19, (uint32_t)isr19, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(20, (uint32_t)isr20, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(21, (uint32_t)isr21, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(22, (uint32_t)isr22, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(23, (uint32_t)isr23, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(24, (uint32_t)isr24, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(25, (uint32_t)isr25, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(26, (uint32_t)isr26, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(27, (uint32_t)isr27, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(28, (uint32_t)isr28, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(29, (uint32_t)isr29, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(30, (uint32_t)isr30, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(31, (uint32_t)isr31, 0x08, IDT_FLAG_INTERRUPT_GATE);

    pic_remap(32, 40);

    idt_set_gate(32, (uint32_t)irq0, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(33, (uint32_t)irq1, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(34, (uint32_t)irq2, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(35, (uint32_t)irq3, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(36, (uint32_t)irq4, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(37, (uint32_t)irq5, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(38, (uint32_t)irq6, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(39, (uint32_t)irq7, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(40, (uint32_t)irq8, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(41, (uint32_t)irq9, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(42, (uint32_t)irq10, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(43, (uint32_t)irq11, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(44, (uint32_t)irq12, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(45, (uint32_t)irq13, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(46, (uint32_t)irq14, 0x08, IDT_FLAG_INTERRUPT_GATE);
    idt_set_gate(47, (uint32_t)irq15, 0x08, IDT_FLAG_INTERRUPT_GATE);

    idt_set_gate(128, (uint32_t)isr128, 0x08, IDT_FLAG_USER_INTERRUPT_GATE);

    idt_load((uint32_t)&idt_ptr);
}

static void syscall_dispatch(struct InterruptFrame *frame) {
    uint32_t *pd = vmm_current_page_directory();

    if (frame->eax == SYS_WRITE) {
        uint32_t user_ptr = frame->ebx;
        uint32_t len = frame->ecx;
        uint32_t i;

        if (len > 4096u) {
            len = 4096u;
        }

        if (!vmm_user_range_accessible(pd, user_ptr, len, 0)) {
            frame->eax = 0xFFFFFFFFu;
            return;
        }

        for (i = 0; i < len; i++) {
            char c = ((const char *)(uintptr_t)user_ptr)[i];
            gfx_putc(c);
        }

        frame->eax = len;
        return;
    }

    if (frame->eax == SYS_READCHAR) {
        uint32_t user_out = frame->ebx;
        char c;

        if (!vmm_user_range_accessible(pd, user_out, 1u, 1)) {
            frame->eax = 0xFFFFFFFFu;
            return;
        }

        if (keyboard_read_char(&c)) {
            *(char *)(uintptr_t)user_out = c;
            frame->eax = 1u;
        } else {
            frame->eax = 0u;
        }
        return;
    }

    if (frame->eax == SYS_EXIT) {
        userland_return_from_syscall(frame->ebx);
    }

    if (frame->eax == SYS_GETTIME) {
        frame->eax = get_ticks();
        return;
    }

    if (frame->eax == SYS_AUDIO_PLAY) {
        frame->eax = (uint32_t)synth_alloc_and_generate(frame->ecx, frame->ebx,
                                                        (WaveType)frame->edx, 200u);
        return;
    }

    frame->eax = 0xFFFFFFFFu;
}

void isr_dispatch(struct InterruptFrame *frame) {
    if (frame->int_no == 128u) {
        syscall_dispatch(frame);
        return;
    }

    if (frame->int_no < 32u) {
        if (frame->int_no == 14u) {
            uint32_t cr2 = read_cr2();
            if (vm_handle_page_fault(cr2, frame->err_code, frame->eip, frame->cs)) {
                return;
            }

            serial_print("PAGE FAULT (unhandled)\n");
            serial_print("CR2: ");
            serial_write_hex(cr2);
            serial_print("\nEIP: ");
            serial_write_hex(frame->eip);
            serial_print("\nERR: ");
            serial_write_hex(frame->err_code);
            serial_print("\nCR3: ");
            serial_write_hex(vmm_get_cr3());
            serial_print("\nMODE: ");
            serial_write_hex(frame->cs & 3u);
            serial_print("\nFLAGS: P=");
            serial_write_hex(frame->err_code & 1u);
            serial_print(" W=");
            serial_write_hex((frame->err_code >> 1) & 1u);
            serial_print(" U=");
            serial_write_hex((frame->err_code >> 2) & 1u);
            serial_print(" RSVD=");
            serial_write_hex((frame->err_code >> 3) & 1u);
            serial_print(" I=");
            serial_write_hex((frame->err_code >> 4) & 1u);
            serial_print("\nPID: 0 (no scheduler yet)\nSystem halted.\n");

            gfx_print("PAGE FAULT (unhandled)\nCR2: ");
            gfx_write_hex(cr2);
            gfx_print("\nEIP: ");
            gfx_write_hex(frame->eip);
            gfx_print("\nERR: ");
            gfx_write_hex(frame->err_code);
            gfx_print("\nSystem halted.");

            for (;;) {
                __asm__ volatile ("cli; hlt");
            }
        }

        gfx_print("CPU EXCEPTION: ");
        gfx_write_hex(frame->int_no);
        gfx_print("\nSystem halted.");

        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }

    if (frame->int_no == 32u) {
        pit_handle_irq();
    }

    if (frame->int_no == 37u) {
        sb16_handle_irq();
        return;
    }

    if (frame->int_no == 33u) {
        keyboard_handle_irq();
    }

    if (frame->int_no == 44u) {
        mouse_handle_irq();
    }

    if (frame->int_no >= 40u && frame->int_no <= 47u) {
        io_out8(PIC2_CMD, PIC_EOI);
    }

    if (frame->int_no >= 32u && frame->int_no <= 47u) {
        io_out8(PIC1_CMD, PIC_EOI);
    }
}
