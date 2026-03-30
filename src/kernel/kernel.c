#include <stdint.h>

#include "audio.h"
#include "capabilities.h"
#include "desktop.h"
#include "gdt.h"
#include "gfx.h"
#include "idt.h"
#include "keyboard.h"
#include "kshell.h"
#include "mouse.h"
#include "multiboot.h"
#include "pmm.h"
#include "pit.h"
#include "serial.h"
#include "userland.h"
#include "vmm.h"

static uint8_t irq_kernel_stack[8192];

void kmain(uint32_t multiboot_magic, uint32_t multiboot_info_addr) {
    uint32_t irq_stack_top = (uint32_t)(irq_kernel_stack + sizeof(irq_kernel_stack));

    gdt_init(irq_stack_top);
    serial_init();
    serial_print("[coffeeOS] kmain entered\n");

    if (multiboot_magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        serial_print("[coffeeOS] bad multiboot magic\n");
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }

    serial_print("[coffeeOS] pmm_init...\n");
    pmm_init(multiboot_info_addr);
    serial_print("[coffeeOS] pmm_init OK\n");
    serial_print("[coffeeOS] pmm pages total=");
    serial_write_hex(pmm_total_pages());
    serial_print(" used=");
    serial_write_hex(pmm_used_pages());
    serial_print(" free=");
    serial_write_hex(pmm_total_pages() - pmm_used_pages());
    serial_print("\n");
    serial_print("[coffeeOS] idt_init...\n");
    idt_init();
    serial_print("[coffeeOS] idt_init OK\n");

    vmm_init(multiboot_info_addr);
    serial_print("[coffeeOS] paging/vm enabled\n");

    gfx_init(multiboot_info_addr);
    serial_print("[coffeeOS] gfx init OK\n");

    pit_init(100u);
    audio_init();
    keyboard_init();
    mouse_init();

    {
        Capability boot_capability = {0, 0xFFFFFFFFu, 0xA11AB1CAull};
        (void)boot_capability;
    }

    tss_set_kernel_stack(irq_stack_top);
    userland_set_boot_info_addr(multiboot_info_addr);

    __asm__ volatile ("sti");
    desktop_run();
    kshell_run();

    serial_print("[coffeeOS] kshell_run returned unexpectedly\n");
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
