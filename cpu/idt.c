#include <stdint.h>

#include "audio.h"
#include "gfx.h"
#include "io.h"
#include "keyboard.h"
#include "mouse.h"
#include "net.h"
#include "net/socket_abi.h"
#include "pit.h"
#include "process.h"
#include "rtl8139.h"
#include "serial.h"
#include "sb16.h"
#include "syscall_numbers.h"
#include "synth.h"
#include "userland.h"
#include "vfs.h"
#include "vmm.h"
#include "vm.h"
#include "dns.h"
#include "icmp.h"
#include "netif.h"
#include "panic.h"
#include "tcp.h"

#define AUTO_SYNC 1

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
static uint32_t irq_fs_sync_countdown;

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

/* Return non-zero when one user pointer range is accessible for this syscall. */
static int syscall_user_range_ok(uint32_t *pd, uint32_t addr, uint32_t len, int write) {
    if (len == 0u) {
        return 1;
    }
    return vmm_user_range_accessible(pd, addr, len, write);
}

static int syscall_copy_user_bytes(uint32_t *pd, uint32_t user_ptr, void *dst, uint32_t len) {
    uint32_t i;

    if (dst == (void *)0 || (len != 0u && !vmm_user_range_accessible(pd, user_ptr, len, 0))) {
        return 0;
    }
    for (i = 0u; i < len; i++) {
        ((uint8_t *)dst)[i] = ((const uint8_t *)(uintptr_t)user_ptr)[i];
    }
    return 1;
}

/* Copy one NUL-terminated user string into a fixed kernel buffer. */
static int syscall_copy_user_path(uint32_t *pd, uint32_t user_ptr, char *dst, uint32_t dst_len) {
    uint32_t i;

    if (user_ptr == 0u || dst == (char *)0 || dst_len == 0u) {
        return 0;
    }
    for (i = 0u; i + 1u < dst_len; i++) {
        if (!vmm_user_range_accessible(pd, user_ptr + i, 1u, 0)) {
            return 0;
        }
        dst[i] = *(const char *)(uintptr_t)(user_ptr + i);
        if (dst[i] == '\0') {
            return 1;
        }
    }
    dst[dst_len - 1u] = '\0';
    return 0;
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

    if (frame->eax == SYS_OPEN) {
        char path[VFS_MAX_PATH];

        if (!syscall_copy_user_path(pd, frame->ebx, path, sizeof(path))) {
            frame->eax = 0xFFFFFFFFu;
            return;
        }
        frame->eax = (uint32_t)vfs_open(path, frame->ecx);
        return;
    }

    if (frame->eax == SYS_CLOSE) {
        frame->eax = (uint32_t)vfs_close((int)frame->ebx);
        return;
    }

    if (frame->eax == SYS_READ) {
        if (!syscall_user_range_ok(pd, frame->ecx, frame->edx, 1)) {
            frame->eax = 0xFFFFFFFFu;
            return;
        }
        frame->eax = (uint32_t)vfs_read((int)frame->ebx, (void *)(uintptr_t)frame->ecx, frame->edx);
        return;
    }

    if (frame->eax == SYS_WRITE_FD) {
        if (!syscall_user_range_ok(pd, frame->ecx, frame->edx, 0)) {
            frame->eax = 0xFFFFFFFFu;
            return;
        }
        frame->eax = (uint32_t)vfs_write((int)frame->ebx, (const void *)(uintptr_t)frame->ecx, frame->edx);
        return;
    }

    if (frame->eax == SYS_SEEK) {
        frame->eax = (uint32_t)vfs_seek((int)frame->ebx, (int32_t)frame->ecx, (int)frame->edx);
        return;
    }

    if (frame->eax == SYS_MKDIR) {
        char path[VFS_MAX_PATH];

        if (!syscall_copy_user_path(pd, frame->ebx, path, sizeof(path))) {
            frame->eax = 0xFFFFFFFFu;
            return;
        }
        frame->eax = (uint32_t)vfs_mkdir(path);
        return;
    }

    if (frame->eax == SYS_DELETE) {
        char path[VFS_MAX_PATH];

        if (!syscall_copy_user_path(pd, frame->ebx, path, sizeof(path))) {
            frame->eax = 0xFFFFFFFFu;
            return;
        }
        frame->eax = (uint32_t)vfs_delete(path);
        return;
    }

    if (frame->eax == SYS_STAT) {
        char path[VFS_MAX_PATH];

        if (!syscall_copy_user_path(pd, frame->ebx, path, sizeof(path))
            || !syscall_user_range_ok(pd, frame->ecx, sizeof(VfsDirEntry), 1)) {
            frame->eax = 0xFFFFFFFFu;
            return;
        }
        frame->eax = (uint32_t)vfs_stat(path, (VfsDirEntry *)(uintptr_t)frame->ecx);
        return;
    }

    if (frame->eax == SYS_LISTDIR) {
        char path[VFS_MAX_PATH];
        uint32_t max = frame->edx;

        if (max > 0u && !syscall_user_range_ok(pd, frame->ecx, max * sizeof(VfsDirEntry), 1)) {
            frame->eax = 0xFFFFFFFFu;
            return;
        }
        if (!syscall_copy_user_path(pd, frame->ebx, path, sizeof(path))) {
            frame->eax = 0xFFFFFFFFu;
            return;
        }
        frame->eax = (uint32_t)vfs_listdir(path, (VfsDirEntry *)(uintptr_t)frame->ecx, (int)max);
        return;
    }

    if (frame->eax == SYS_SOCKET || frame->eax == SYS_BIND || frame->eax == SYS_LISTEN
        || frame->eax == SYS_ACCEPT || frame->eax == SYS_CONNECT || frame->eax == SYS_SEND
        || frame->eax == SYS_RECV || frame->eax == SYS_SENDTO || frame->eax == SYS_RECVFROM
        || frame->eax == SYS_SOCKET_CLOSE) {
        frame->eax = 0xFFFFFFFFu;
        return;
    }

    if (frame->eax == SYS_NET_DNS) {
        char host[DNS_MAX_NAME];
        uint32_t ip;

        if (!syscall_copy_user_path(pd, frame->ebx, host, sizeof(host))
            || !syscall_user_range_ok(pd, frame->ecx, sizeof(uint32_t), 1)) {
            frame->eax = 0xFFFFFFFFu;
            return;
        }
        frame->eax = (dns_resolve(host, &ip) == 0) ? 0u : 0xFFFFFFFFu;
        if (frame->eax == 0u) {
            *(uint32_t *)(uintptr_t)frame->ecx = ip;
        }
        return;
    }

    if (frame->eax == SYS_NET_PING) {
        NetInterface *iface = netif_default();
        int rc = -1;
        uint32_t i;

        if (iface == (NetInterface *)0) {
            frame->eax = 0xFFFFFFFFu;
            return;
        }
        for (i = 0u; i < frame->ecx; i++) {
            if (icmp_ping(iface, frame->ebx, (uint16_t)i, 100u) == 0) {
                rc = 0;
            }
        }
        frame->eax = (rc == 0) ? get_ticks() : 0xFFFFFFFFu;
        return;
    }

    if (frame->eax == SYS_TCP_CONNECT) {
        NetInterface *iface = netif_default();

        frame->eax = (iface == (NetInterface *)0)
            ? 0xFFFFFFFFu
            : (uint32_t)tcp_connect(iface, frame->ebx, (uint16_t)frame->ecx, 300u);
        return;
    }

    if (frame->eax == SYS_TCP_SEND) {
        if (!syscall_user_range_ok(pd, frame->ecx, frame->edx, 0)) {
            frame->eax = 0xFFFFFFFFu;
            return;
        }
        frame->eax = (uint32_t)tcp_send((int)frame->ebx, (const void *)(uintptr_t)frame->ecx, (uint16_t)frame->edx);
        return;
    }

    if (frame->eax == SYS_TCP_RECV) {
        if (!syscall_user_range_ok(pd, frame->ecx, frame->edx, 1)) {
            frame->eax = 0xFFFFFFFFu;
            return;
        }
        frame->eax = (uint32_t)tcp_recv((int)frame->ebx, (void *)(uintptr_t)frame->ecx, (uint16_t)frame->edx, 300u);
        return;
    }

    if (frame->eax == SYS_TCP_CLOSE) {
        tcp_close((int)frame->ebx);
        frame->eax = 0u;
        return;
    }

    if (frame->eax == SYS_NET_STAT) {
        NetInterface *iface = netif_get(0);

        if (iface == (NetInterface *)0 || !syscall_user_range_ok(pd, frame->ebx, sizeof(NetInterface), 1)) {
            frame->eax = 0xFFFFFFFFu;
            return;
        }
        *(NetInterface *)(uintptr_t)frame->ebx = *iface;
        frame->eax = 0u;
        return;
    }

    if (frame->eax == SYS_GETPID) {
        frame->eax = process_current_pid();
        return;
    }

    if (frame->eax == SYS_YIELD) {
        frame->eax = 0u;
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

            if ((frame->cs & 3u) == 3u && process_current() != (struct Process *)0) {
                serial_print("USER PAGE FAULT: terminating process\n");
                serial_print("CR2: ");
                serial_write_hex(cr2);
                serial_print(" EIP: ");
                serial_write_hex(frame->eip);
                serial_print(" ERR: ");
                serial_write_hex(frame->err_code);
                serial_print("\n");
                userland_fault_current_process(0x80u | frame->int_no);
                return;
            }

            panic_dump(frame, cr2);
            for (;;) {
                __asm__ volatile ("cli; hlt");
            }
        }

        if ((frame->cs & 3u) == 3u && process_current() != (struct Process *)0) {
            serial_print("USER EXCEPTION: terminating process int=");
            serial_write_hex(frame->int_no);
            serial_print("\n");
            userland_fault_current_process(0x80u | frame->int_no);
            return;
        }

        panic_dump(frame, 0u);
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }

    if (frame->int_no == 32u) {
        pit_handle_irq();
        net_tick();
#if AUTO_SYNC
        irq_fs_sync_countdown++;
        if (irq_fs_sync_countdown >= 500u) {
            pit_request_fs_sync();
            irq_fs_sync_countdown = 0u;
        }
#endif
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

    if (frame->int_no >= 32u && frame->int_no <= 47u) {
        if (rtl8139_present() && rtl8139_irq_line() == (int)(frame->int_no - 32u)) {
            rtl8139_irq();
        }
    }

    if (frame->int_no >= 40u && frame->int_no <= 47u) {
        io_out8(PIC2_CMD, PIC_EOI);
    }

    if (frame->int_no >= 32u && frame->int_no <= 47u) {
        io_out8(PIC1_CMD, PIC_EOI);
    }
}
