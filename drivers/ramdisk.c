#include <stdint.h>

#include "blkdev.h"
#include "paging.h"
#include "pmm.h"
#include "ramdisk.h"
#include "serial.h"
#include "vmm.h"

#define RAMDISK_PAGE_COUNT (RAMDISK_SIZE / PAGE_SIZE)

static uint32_t ramdisk_pages[RAMDISK_PAGE_COUNT];
static uint8_t *ramdisk_base = (uint8_t *)(uintptr_t)RAMDISK_VIRT_BASE;
static BlockDevice ramdisk_device;
static int ramdisk_ready;
static int ramdisk_preloaded;
static uint32_t ramdisk_module_phys;
static uint32_t ramdisk_module_size;

/* Copy a raw byte range between two already-mapped kernel buffers. */
static void ramdisk_memcpy(void *dst, const void *src, uint32_t len) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    uint32_t i;

    for (i = 0u; i < len; i++) {
        d[i] = s[i];
    }
}

/* Fill one raw byte range with zeroes. */
static void ramdisk_memzero(void *dst, uint32_t len) {
    uint8_t *d = (uint8_t *)dst;
    uint32_t i;

    for (i = 0u; i < len; i++) {
        d[i] = 0u;
    }
}

/* Copy one physical source range into the mapped ramdisk window a page at a time. */
static int memcpy32(uint32_t dst_virt, uint32_t src_phys, uint32_t size) {
    uint32_t copied = 0u;

    while (copied < size) {
        uint32_t page_phys = (src_phys + copied) & ~0xFFFu;
        uint32_t page_off = (src_phys + copied) & 0xFFFu;
        uint32_t chunk = PAGE_SIZE - page_off;
        uint8_t *src_window;
        uint8_t *dst;
        uint32_t words;
        uint32_t bytes;
        uint32_t i;

        if (chunk > size - copied) {
            chunk = size - copied;
        }
        if (!vmm_map_page(RAMDISK_COPY_WINDOW, page_phys, VMM_FLAG_PRESENT | VMM_FLAG_WRITE)) {
            return -1;
        }

        src_window = (uint8_t *)(uintptr_t)(RAMDISK_COPY_WINDOW + page_off);
        dst = (uint8_t *)(uintptr_t)(dst_virt + copied);
        words = chunk / 4u;
        bytes = chunk % 4u;

        for (i = 0u; i < words; i++) {
            ((uint32_t *)dst)[i] = ((const uint32_t *)src_window)[i];
        }
        for (i = 0u; i < bytes; i++) {
            dst[(words * 4u) + i] = src_window[(words * 4u) + i];
        }

        (void)vmm_unmap_page(RAMDISK_COPY_WINDOW);
        copied += chunk;
    }
    return 0;
}

/* Copy the mapped ramdisk window back into one physical destination range a page at a time. */
static int memcpy32_to_phys(uint32_t dst_phys, uint32_t src_virt, uint32_t size) {
    uint32_t copied = 0u;

    while (copied < size) {
        uint32_t page_phys = (dst_phys + copied) & ~0xFFFu;
        uint32_t page_off = (dst_phys + copied) & 0xFFFu;
        uint32_t chunk = PAGE_SIZE - page_off;
        uint8_t *dst_window;
        const uint8_t *src;
        uint32_t words;
        uint32_t bytes;
        uint32_t i;

        if (chunk > size - copied) {
            chunk = size - copied;
        }
        if (!vmm_map_page(RAMDISK_COPY_WINDOW, page_phys, VMM_FLAG_PRESENT | VMM_FLAG_WRITE)) {
            return -1;
        }

        dst_window = (uint8_t *)(uintptr_t)(RAMDISK_COPY_WINDOW + page_off);
        src = (const uint8_t *)(uintptr_t)(src_virt + copied);
        words = chunk / 4u;
        bytes = chunk % 4u;

        for (i = 0u; i < words; i++) {
            ((uint32_t *)dst_window)[i] = ((const uint32_t *)src)[i];
        }
        for (i = 0u; i < bytes; i++) {
            dst_window[(words * 4u) + i] = src[(words * 4u) + i];
        }

        (void)vmm_unmap_page(RAMDISK_COPY_WINDOW);
        copied += chunk;
    }
    return 0;
}

/* Read a block-aligned range from the mapped ramdisk window. */
static int ramdisk_read(BlockDevice *dev, uint32_t lba, uint32_t count, void *buf) {
    uint32_t byte_offset;
    uint32_t byte_len;

    (void)dev;
    if (!ramdisk_ready || buf == (void *)0) {
        return -1;
    }
    if (count == 0u || lba >= RAMDISK_BLOCKS || count > RAMDISK_BLOCKS - lba) {
        return -1;
    }

    byte_offset = lba * BLOCK_SIZE;
    byte_len = count * BLOCK_SIZE;
    ramdisk_memcpy(buf, ramdisk_base + byte_offset, byte_len);
    return 0;
}

/* Write a block-aligned range into the mapped ramdisk window. */
static int ramdisk_write(BlockDevice *dev, uint32_t lba, uint32_t count, const void *buf) {
    uint32_t byte_offset;
    uint32_t byte_len;

    (void)dev;
    if (!ramdisk_ready || buf == (const void *)0) {
        return -1;
    }
    if (count == 0u || lba >= RAMDISK_BLOCKS || count > RAMDISK_BLOCKS - lba) {
        return -1;
    }

    byte_offset = lba * BLOCK_SIZE;
    byte_len = count * BLOCK_SIZE;
    ramdisk_memcpy(ramdisk_base + byte_offset, buf, byte_len);
    return 0;
}

/* Return non-zero when the ramdisk booted from a preloaded disk image. */
int ramdisk_has_image(void) {
    return ramdisk_preloaded;
}

/* Zero the entire mapped ramdisk after it has been initialized. */
void ramdisk_zero(void) {
    if (!ramdisk_ready) {
        return;
    }
    ramdisk_memzero(ramdisk_base, RAMDISK_SIZE);
}

/* Copy an already-mapped disk image into the ramdisk from LBA 0 upward. */
int ramdisk_load_from_memory(const uint8_t *src, uint32_t size) {
    if (!ramdisk_ready || src == (const uint8_t *)0 || size > RAMDISK_SIZE) {
        return -1;
    }

    ramdisk_memcpy(ramdisk_base, src, size);
    if (size < RAMDISK_SIZE) {
        ramdisk_memzero(ramdisk_base + size, RAMDISK_SIZE - size);
    }
    return 0;
}

/* Copy the ramdisk mapping back into the original multiboot module pages. */
int ramdisk_save_to_module(uint32_t dest_phys, uint32_t size) {
    if (!ramdisk_ready || dest_phys == 0u || size == 0u) {
        return -1;
    }
    if (size > RAMDISK_SIZE) {
        size = RAMDISK_SIZE;
    }
    return memcpy32_to_phys(dest_phys, RAMDISK_VIRT_BASE, size);
}

/* Persist the ramdisk back to its boot module when one was preloaded. */
int ramdisk_sync_backing_store(void) {
    if (!ramdisk_preloaded || ramdisk_module_phys == 0u || ramdisk_module_size == 0u) {
        return -1;
    }
    return ramdisk_save_to_module(ramdisk_module_phys, ramdisk_module_size);
}

/* Allocate pages, map them contiguously, and preload an optional persistent image. */
int ramdisk_init(uint32_t preload_phys_addr, uint32_t preload_size) {
    uint32_t i;
    int reg_index;

    if (ramdisk_ready) {
        return 0;
    }

    for (i = 0u; i < RAMDISK_PAGE_COUNT; i++) {
        uint32_t phys = pmm_alloc_page_phys();

        if (phys == 0u) {
            serial_print("[ramdisk] failed to allocate page\n");
            return -1;
        }
        ramdisk_pages[i] = phys;
    }

    for (i = 0u; i < RAMDISK_PAGE_COUNT; i++) {
        if (!vmm_map_page(RAMDISK_VIRT_BASE + (i * PAGE_SIZE), ramdisk_pages[i],
                          VMM_FLAG_PRESENT | VMM_FLAG_WRITE)) {
            serial_print("[ramdisk] failed to map page\n");
            return -1;
        }
    }

    ramdisk_ready = 1;
    ramdisk_preloaded = 0;
    ramdisk_module_phys = 0u;
    ramdisk_module_size = 0u;
    ramdisk_zero();

    if (preload_phys_addr != 0u) {
        if (preload_size == 0u || preload_size > RAMDISK_SIZE) {
            serial_print("[ramdisk] preloaded image too large\n");
            return -1;
        }
        if (memcpy32(RAMDISK_VIRT_BASE, preload_phys_addr, preload_size) != 0) {
            serial_print("[ramdisk] failed to map preload image\n");
            return -1;
        }
        if (preload_size < RAMDISK_SIZE) {
            ramdisk_memzero(ramdisk_base + preload_size, RAMDISK_SIZE - preload_size);
        }
        ramdisk_preloaded = 1;
        ramdisk_module_phys = preload_phys_addr;
        ramdisk_module_size = preload_size;
        serial_print("[ramdisk] loaded persistent disk image: ");
        serial_write_hex(preload_size);
        serial_print(" bytes\n");
    } else {
        serial_print("[ramdisk] no preloaded image, starting blank\n");
    }

    ramdisk_memzero(&ramdisk_device, sizeof(ramdisk_device));
    ramdisk_device.name[0] = 'r';
    ramdisk_device.name[1] = 'a';
    ramdisk_device.name[2] = 'm';
    ramdisk_device.name[3] = '0';
    ramdisk_device.name[4] = '\0';
    ramdisk_device.block_count = RAMDISK_BLOCKS;
    ramdisk_device.block_size = BLOCK_SIZE;
    ramdisk_device.read = ramdisk_read;
    ramdisk_device.write = ramdisk_write;
    ramdisk_device.private_data = (void *)0;
    ramdisk_device.present = 1;

    reg_index = blkdev_register(&ramdisk_device);
    if (reg_index < 0) {
        serial_print("[ramdisk] failed to register block device\n");
        return -1;
    }

    serial_print("[ramdisk] initialized 16MB at virt 0xD8000000\n");
    return 0;
}
