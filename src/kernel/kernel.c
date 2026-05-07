#include <stdint.h>

#include "ascii_util.h"
#include "ata.h"
#include "audio.h"
#include "blkdev.h"
#include "boot_animation.h"
#include "capabilities.h"
#include "desktop.h"
#include "fat32.h"
#include "fat32_format.h"
#include "gdt.h"
#include "gfx.h"
#include "idt.h"
#include "keyboard.h"
#include "kshell.h"
#include "mouse.h"
#include "mbr.h"
#include "multiboot.h"
#include "net.h"
#include "pmm.h"
#include "pit.h"
#include "process.h"
#include "ramdisk.h"
#include "serial.h"
#include "userland.h"
#include "vfs.h"
#include "vmm.h"

static uint8_t irq_kernel_stack[32768];
static uint32_t disk_mod_start = 0u;
static uint32_t disk_mod_size = 0u;

/* Scan Multiboot modules, find the persistent disk image, and reserve all module pages. */
static void scan_multiboot_modules(uint32_t info_addr) {
    struct MultibootInfo *mb = (struct MultibootInfo *)(uintptr_t)info_addr;
    struct MultibootModule *mods;
    uint32_t i;

    if ((mb->flags & MULTIBOOT_INFO_MODS) == 0u || mb->mods_count == 0u) {
        serial_print("[boot] no multiboot modules found\n");
        return;
    }

    mods = (struct MultibootModule *)(uintptr_t)mb->mods_addr;
    for (i = 0u; i < mb->mods_count; i++) {
        const char *cmdline = mods[i].string != 0u ? (const char *)(uintptr_t)mods[i].string : "";
        uint32_t page = mods[i].mod_start & ~0xFFFu;
        uint32_t end = (mods[i].mod_end + 0xFFFu) & ~0xFFFu;

        serial_print("[boot] module: ");
        serial_print(cmdline);
        serial_print("\n");
        if (ascii_streq(cmdline, "disk") || ascii_ends_with(cmdline, "disk.img")) {
            disk_mod_start = mods[i].mod_start;
            disk_mod_size = mods[i].mod_end - mods[i].mod_start;
            serial_print("[boot] found disk module at ");
            serial_write_hex(disk_mod_start);
            serial_print(" size=");
            serial_write_hex(disk_mod_size);
            serial_print("\n");
        }
        while (page < end) {
            pmm_mark_used((void *)(uintptr_t)page);
            page += 4096u;
        }
    }
}

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
    scan_multiboot_modules(multiboot_info_addr);
    serial_print("[coffeeOS] idt_init...\n");
    idt_init();
    serial_print("[coffeeOS] idt_init OK\n");

    vmm_init(multiboot_info_addr);
    serial_print("[coffeeOS] paging/vm enabled\n");

    gfx_init(multiboot_info_addr);
    serial_print("[coffeeOS] gfx init OK\n");
    process_init();

    pit_init(100u);
    audio_init();
    keyboard_init();
    mouse_init();
    tss_set_kernel_stack(irq_stack_top);
    __asm__ volatile ("sti");
    boot_animation_run();
    net_init();
    blkdev_init();
    (void)ata_init();
    {
        BlockDevice *hd0_scan = blkdev_find("hd0");

        if (hd0_scan != (BlockDevice *)0) {
            (void)mbr_register_partitions(hd0_scan);
        }
    }
    if (ramdisk_init(disk_mod_start, disk_mod_size) != 0) {
        serial_print("[vfs] ramdisk init failed\n");
    }
    {
        BlockDevice *hd0 = blkdev_find("hd0");
        BlockDevice *hd0p1 = blkdev_find("hd0p1");
        BlockDevice *ram0 = blkdev_find("ram0");
        BlockDevice *boot_dev = (BlockDevice *)0;
        int vol_idx = 0;

        vfs_init();
        if (hd0p1 != (BlockDevice *)0) {
            serial_print("[boot] ATA partition detected, attempting FAT32 mount\n");
            if (fat32_mount(vol_idx, hd0p1) == 0) {
                serial_print("[boot] mounted hd0p1 as FAT32\n");
                boot_dev = hd0p1;
                goto mount_ok;
            }
            serial_print("[boot] hd0p1 FAT32 mount failed, falling back\n");
        }
        if (hd0 != (BlockDevice *)0) {
            serial_print("[boot] ATA drive detected, attempting whole-disk FAT32 mount\n");
            if (fat32_mount(vol_idx, hd0) == 0) {
                serial_print("[boot] mounted hd0 as FAT32\n");
                boot_dev = hd0;
                goto mount_ok;
            }
            serial_print("[boot] hd0 FAT32 mount failed, falling back to ramdisk\n");
        }

        if (ram0 == (BlockDevice *)0) {
            serial_print("[vfs] ram0 not found\n");
        } else {
            boot_dev = ram0;
            if (ramdisk_has_image()) {
                serial_print("[boot] mounting persistent ramdisk image\n");
                if (fat32_mount(vol_idx, ram0) != 0) {
                    serial_print("[boot] ramdisk mount failed, reformatting\n");
                    if (fat32_format(ram0, "COFFEEOS") != 0 || fat32_mount(vol_idx, ram0) != 0
                        || vfs_create_default_dirs() != VFS_OK) {
                        serial_print("[vfs] ramdisk recovery failed\n");
                    }
                }
            } else {
                serial_print("[boot] fresh ramdisk, formatting\n");
                if (fat32_format(ram0, "COFFEEOS") != 0 || fat32_mount(vol_idx, ram0) != 0
                    || vfs_create_default_dirs() != VFS_OK) {
                    serial_print("[vfs] fresh ramdisk format failed\n");
                }
            }
        }

mount_ok:
        if (boot_dev != (BlockDevice *)0 && fat32_get_volume(vol_idx) != (Fat32Volume *)0) {
            int fsck_result = fat32_fsck(vol_idx);

            serial_print("[vfs] fsck result=");
            serial_write_hex((uint32_t)fsck_result);
            serial_print("\n");
            serial_print("[vfs] filesystem ready\n");
        }
    }

    {
        Capability boot_capability = {0, 0xFFFFFFFFu, 0xA11AB1CAull};
        (void)boot_capability;
    }

    userland_set_boot_info_addr(multiboot_info_addr);
    desktop_run();
    kshell_run();

    serial_print("[coffeeOS] kshell_run returned unexpectedly\n");
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
