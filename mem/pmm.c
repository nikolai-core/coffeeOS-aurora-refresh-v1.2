#include <stdint.h>

#include "multiboot.h"
#include "pmm.h"
#include "serial.h"

#define PAGE_SIZE 4096u
#define MAX_MEMORY_BYTES (4ULL * 1024ULL * 1024ULL * 1024ULL)
#define MAX_PAGES ((uint32_t)(MAX_MEMORY_BYTES / PAGE_SIZE))
#define BITMAP_WORDS (MAX_PAGES / 32u)

extern uint8_t _kernel_phys_start;
extern uint8_t _kernel_phys_end;

static uint32_t bitmap[BITMAP_WORDS];
static uint16_t refcount[MAX_PAGES];
static uint32_t total_pages;
static uint32_t used_pages;
static uint32_t memmap_addr_saved;
static uint32_t memmap_length_saved;
static uint32_t first_free_hint;

static void mark_region_used(uint32_t base, uint32_t length);
static uint32_t bitmap_test(uint32_t page);

static uint32_t pages_from_end_u64(uint64_t end64) {
    if (end64 == 0ULL) {
        return 0u;
    }
    if (end64 > 0x100000000ULL) {
        end64 = 0x100000000ULL;
    }
    return (uint32_t)((end64 + PAGE_SIZE - 1u) / PAGE_SIZE);
}

static uint32_t recount_used_pages(uint32_t page_limit) {
    uint32_t page;
    uint32_t count = 0u;

    for (page = 0u; page < page_limit; page++) {
        if (bitmap_test(page)) {
            count++;
        }
    }
    return count;
}

static uint32_t clamp_u64_to_u32(uint64_t value) {
    if (value > 0xFFFFFFFFULL) {
        return 0xFFFFFFFFu;
    }
    return (uint32_t)value;
}

static void mark_region_used_u64(uint64_t base64, uint64_t length64) {
    uint32_t base;
    uint32_t length;

    if (length64 == 0ULL) {
        return;
    }

    if (base64 >= 0x100000000ULL) {
        return;
    }

    if (base64 + length64 > 0x100000000ULL) {
        length64 = 0x100000000ULL - base64;
    }

    base = (uint32_t)base64;
    length = clamp_u64_to_u32(length64);
    if (length == 0u) {
        return;
    }

    mark_region_used(base, length);
}

static void bitmap_set(uint32_t page) {
    bitmap[page / 32u] |= (1u << (page % 32u));
}

static void bitmap_clear(uint32_t page) {
    bitmap[page / 32u] &= ~(1u << (page % 32u));
}

static uint32_t bitmap_test(uint32_t page) {
    return (bitmap[page / 32u] >> (page % 32u)) & 1u;
}

static void mark_region_used(uint32_t base, uint32_t length) {
    uint32_t start_page;
    uint32_t end_page;
    uint32_t p;

    if (length == 0) {
        return;
    }

    start_page = base / PAGE_SIZE;
    end_page = (base + length - 1u) / PAGE_SIZE;

    if (start_page >= total_pages) {
        return;
    }

    if (end_page >= total_pages) {
        end_page = total_pages - 1u;
    }

    for (p = start_page; p <= end_page; p++) {
        if (!bitmap_test(p)) {
            bitmap_set(p);
            used_pages++;
        }
    }
}

static void mark_region_free(uint32_t base, uint32_t length) {
    uint32_t start_page;
    uint32_t end_page;
    uint32_t p;

    if (length == 0) {
        return;
    }

    start_page = base / PAGE_SIZE;
    end_page = (base + length - 1u) / PAGE_SIZE;

    if (start_page >= total_pages) {
        return;
    }

    if (end_page >= total_pages) {
        end_page = total_pages - 1u;
    }

    for (p = start_page; p <= end_page; p++) {
        if (bitmap_test(p)) {
            bitmap_clear(p);
            used_pages--;
        }
    }
}

void pmm_init(uint32_t multiboot_info_addr) {
    uint32_t i;
    struct MultibootInfo *mbi = (struct MultibootInfo *)multiboot_info_addr;
    uint32_t entries_seen = 0;
    uint32_t available_entries = 0;
    uint32_t detected_pages = 0u;

    total_pages = MAX_PAGES;
    used_pages = 0;
    memmap_addr_saved = 0u;
    memmap_length_saved = 0u;
    first_free_hint = 0u;

    for (i = 0; i < BITMAP_WORDS; i++) {
        bitmap[i] = 0xFFFFFFFFu;
    }
    for (i = 0; i < MAX_PAGES; i++) {
        refcount[i] = 0u;
    }
    used_pages = total_pages;

    if ((mbi->flags & MULTIBOOT_INFO_MEM_MAP) != 0) {
        uint32_t end = mbi->mmap_addr + mbi->mmap_length;
        uint32_t current = mbi->mmap_addr;

        memmap_addr_saved = mbi->mmap_addr;
        memmap_length_saved = mbi->mmap_length;

        serial_print("[coffeeOS] pmm: mmap_addr=");
        serial_write_hex(mbi->mmap_addr);
        serial_print(" mmap_len=");
        serial_write_hex(mbi->mmap_length);
        serial_print("\n");

        while (current < end) {
            struct MultibootMMapEntry *entry = (struct MultibootMMapEntry *)current;

            if (entries_seen < 8u) {
                serial_print("[coffeeOS] pmm: e");
                serial_write_hex(entries_seen);
                serial_print(" size=");
                serial_write_hex(entry->size);
                serial_print(" type=");
                serial_write_hex(entry->type);
                serial_print(" addr_lo=");
                serial_write_hex((uint32_t)entry->addr);
                serial_print(" len_lo=");
                serial_write_hex((uint32_t)entry->len);
                serial_print("\n");
            }
            entries_seen++;

            if (entry->type == 1 && entry->len != 0) {
                uint64_t region_start64 = entry->addr;
                uint64_t region_end64 = entry->addr + entry->len;
                uint32_t region_pages = pages_from_end_u64(region_end64);

                if (region_pages > detected_pages) {
                    detected_pages = region_pages;
                }

                if (region_start64 < 0x100000000ULL) {
                    uint32_t region_start = (uint32_t)region_start64;
                    uint32_t region_len;

                    if (region_end64 > 0x100000000ULL) {
                        region_end64 = 0x100000000ULL;
                    }

                    region_len = (uint32_t)(region_end64 - region_start64);
                    mark_region_free(region_start, region_len);
                    available_entries++;
                }
            }

            current += entry->size + sizeof(entry->size);
        }

        serial_print("[coffeeOS] pmm: entries_seen=");
        serial_write_hex(entries_seen);
        serial_print(" avail_entries=");
        serial_write_hex(available_entries);
        serial_print("\n");
    }

    if (available_entries == 0u && (mbi->flags & 1u) != 0u) {
        uint64_t low_len = (uint64_t)mbi->mem_lower * 1024ULL;
        uint64_t high_len = (uint64_t)mbi->mem_upper * 1024ULL;
        uint64_t high_start = 0x100000ULL;
        uint64_t highest_end = low_len;

        serial_print("[coffeeOS] pmm: mmap unusable; falling back to mem_lower/mem_upper\n");
        serial_print("[coffeeOS] pmm: mem_lower_kb=");
        serial_write_hex(mbi->mem_lower);
        serial_print(" mem_upper_kb=");
        serial_write_hex(mbi->mem_upper);
        serial_print("\n");

        if (low_len != 0ULL) {
            if (low_len > 0x100000000ULL) {
                low_len = 0x100000000ULL;
            }
            mark_region_free(0u, (uint32_t)low_len);
        }

        if (high_len != 0ULL && high_start < 0x100000000ULL) {
            uint64_t high_end = high_start + high_len;
            if (high_end > 0x100000000ULL) {
                high_end = 0x100000000ULL;
            }
            if (high_end > highest_end) {
                highest_end = high_end;
            }
            mark_region_free((uint32_t)high_start, (uint32_t)(high_end - high_start));
        }

        detected_pages = pages_from_end_u64(highest_end);
    }

    if (detected_pages == 0u || detected_pages > MAX_PAGES) {
        detected_pages = MAX_PAGES;
    }
    total_pages = detected_pages;
    used_pages = recount_used_pages(total_pages);

    mark_region_used(0, 0x100000u);
    mark_region_used((uint32_t)(uintptr_t)&_kernel_phys_start,
                     (uint32_t)(uintptr_t)(&_kernel_phys_end) - (uint32_t)(uintptr_t)(&_kernel_phys_start));

    /* Reserve multiboot structures and modules so we don't overwrite them. */
    mark_region_used(multiboot_info_addr, (uint32_t)sizeof(*mbi));
    if ((mbi->flags & MULTIBOOT_INFO_MEM_MAP) != 0u) {
        mark_region_used(mbi->mmap_addr, mbi->mmap_length);
    }
    if ((mbi->flags & MULTIBOOT_INFO_MODS) != 0u && mbi->mods_count != 0u) {
        uint32_t mods_size = mbi->mods_count * (uint32_t)sizeof(struct MultibootModule);
        uint32_t m;
        mark_region_used(mbi->mods_addr, mods_size);
        for (m = 0; m < mbi->mods_count; m++) {
            struct MultibootModule *mods = (struct MultibootModule *)(uintptr_t)mbi->mods_addr;
            uint32_t start = mods[m].mod_start;
            uint32_t end = mods[m].mod_end;
            if (end > start) {
                mark_region_used(start, end - start);
            }
            if (mods[m].string != 0u) {
                mark_region_used(mods[m].string, 256u);
            }
        }
    }
    if ((mbi->flags & MULTIBOOT_INFO_CMDLINE) != 0u && mbi->cmdline != 0u) {
        mark_region_used(mbi->cmdline, 256u);
    }
    if ((mbi->flags & MULTIBOOT_INFO_BOOT_LOADER_NAME) != 0u && mbi->boot_loader_name != 0u) {
        mark_region_used(mbi->boot_loader_name, 256u);
    }
    if ((mbi->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO) != 0u) {
        uint64_t fb_addr64 = mbi->framebuffer_addr;
        uint64_t fb_size64 = (uint64_t)mbi->framebuffer_pitch * (uint64_t)mbi->framebuffer_height;
        mark_region_used_u64(fb_addr64, fb_size64);
    }

    serial_print("[coffeeOS] pmm: used_pages=");
    serial_write_hex(used_pages);
    serial_print(" free_pages=");
    serial_write_hex(total_pages - used_pages);
    serial_print("\n");
}

uint32_t pmm_alloc_page_below(uint32_t max_phys) {
    uint32_t max_page = max_phys / PAGE_SIZE;
    uint32_t word_limit = (max_page + 31u) / 32u;
    uint32_t start_page;
    uint32_t word;

    if (max_page == 0u) {
        return 0u;
    }
    if (word_limit > BITMAP_WORDS) {
        word_limit = BITMAP_WORDS;
    }

    /* Start from the lowest page that could still be free instead of rescanning from zero. */
    start_page = first_free_hint;
    if (start_page >= max_page) {
        start_page = 0u;
    }

    for (word = start_page / 32u; word < word_limit; word++) {
        uint32_t bits = bitmap[word];
        if (bits != 0xFFFFFFFFu) {
            uint32_t bit;
            for (bit = 0; bit < 32u; bit++) {
                uint32_t page = word * 32u + bit;
                if (page < start_page) {
                    continue;
                }
                if (page >= max_page) {
                    break;
                }
                if (((bits >> bit) & 1u) == 0u) {
                    bitmap_set(page);
                    used_pages++;
                    refcount[page] = 1u;
                    /* Keep the next allocation scan close to the last free run we found. */
                    first_free_hint = page + 1u;
                    return page * PAGE_SIZE;
                }
            }
        }
    }

    for (word = 0u; word < start_page / 32u && word < word_limit; word++) {
        uint32_t bits = bitmap[word];
        if (bits != 0xFFFFFFFFu) {
            uint32_t bit;
            for (bit = 0; bit < 32u; bit++) {
                uint32_t page = word * 32u + bit;
                if (page >= max_page) {
                    break;
                }
                if (((bits >> bit) & 1u) == 0u) {
                    bitmap_set(page);
                    used_pages++;
                    refcount[page] = 1u;
                    first_free_hint = page + 1u;
                    return page * PAGE_SIZE;
                }
            }
        }
    }

    return 0u;
}

void *pmm_alloc_page(void) {
    uint32_t start_page = first_free_hint;
    uint32_t word;

    /* Remember the lowest page that might be free so allocs stop rescanning page zero. */
    for (word = start_page / 32u; word < BITMAP_WORDS; word++) {
        uint32_t bits = bitmap[word];
        if (bits != 0xFFFFFFFFu) {
            uint32_t bit;
            for (bit = 0; bit < 32u; bit++) {
                uint32_t page = word * 32u + bit;
                if (page < start_page) {
                    continue;
                }
                if (((bits >> bit) & 1u) == 0) {
                    if (page >= total_pages) {
                        return (void *)0;
                    }
                    bitmap_set(page);
                    used_pages++;
                    refcount[page] = 1u;
                    first_free_hint = page + 1u;
                    return (void *)(page * PAGE_SIZE);
                }
            }
        }
    }

    for (word = 0u; word < start_page / 32u; word++) {
        uint32_t bits = bitmap[word];
        if (bits != 0xFFFFFFFFu) {
            uint32_t bit;
            for (bit = 0; bit < 32u; bit++) {
                uint32_t page = word * 32u + bit;
                if (((bits >> bit) & 1u) == 0u) {
                    if (page >= total_pages) {
                        return (void *)0;
                    }
                    bitmap_set(page);
                    used_pages++;
                    refcount[page] = 1u;
                    first_free_hint = page + 1u;
                    return (void *)(page * PAGE_SIZE);
                }
            }
        }
    }

    return (void *)0;
}

void pmm_free_page(void *addr) {
    uint32_t page = ((uint32_t)addr) / PAGE_SIZE;

    if (page >= total_pages) {
        return;
    }
    if (!bitmap_test(page)) {
        return;
    }

    if (refcount[page] > 1u) {
        refcount[page]--;
        return;
    }
    if (refcount[page] == 1u) {
        refcount[page] = 0u;
        bitmap_clear(page);
        used_pages--;
        if (page < first_free_hint) {
            /* Let the next allocation restart from this newly freed lower page. */
            first_free_hint = page;
        }
    }
}

uint32_t pmm_total_pages(void) {
    return total_pages;
}

uint32_t pmm_used_pages(void) {
    return used_pages;
}

uint32_t pmm_alloc_page_phys(void) {
    return (uint32_t)(uintptr_t)pmm_alloc_page();
}

void pmm_free_page_phys(uint32_t phys_addr) {
    pmm_free_page((void *)(uintptr_t)phys_addr);
}

void pmm_ref_inc(uint32_t phys_addr) {
    uint32_t page = phys_addr / PAGE_SIZE;
    if (page >= total_pages) {
        return;
    }
    if (!bitmap_test(page)) {
        return;
    }
    if (refcount[page] == 0u) {
        refcount[page] = 1u;
        return;
    }
    if (refcount[page] != 0xFFFFu) {
        refcount[page]++;
    }
}

void pmm_ref_dec(uint32_t phys_addr) {
    uint32_t page = phys_addr / PAGE_SIZE;
    if (page >= total_pages) {
        return;
    }
    if (!bitmap_test(page)) {
        return;
    }
    if (refcount[page] == 0u) {
        return;
    }
    if (refcount[page] > 1u) {
        refcount[page]--;
        return;
    }
    refcount[page] = 0u;
    bitmap_clear(page);
    used_pages--;
    if (page < first_free_hint) {
        first_free_hint = page;
    }
}

uint32_t pmm_ref_count(uint32_t phys_addr) {
    uint32_t page = phys_addr / PAGE_SIZE;
    if (page >= total_pages) {
        return 0u;
    }
    return (uint32_t)refcount[page];
}

/* Expose the saved Multiboot memory map one entry at a time for `memmap`. */
int pmm_memmap_next(uint32_t *cursor, struct PmmMemoryRegion *out) {
    struct MultibootMMapEntry *entry;

    if (cursor == (uint32_t *)0 || out == (struct PmmMemoryRegion *)0) {
        return 0;
    }
    if (memmap_addr_saved == 0u || *cursor >= memmap_length_saved) {
        return 0;
    }

    entry = (struct MultibootMMapEntry *)(uintptr_t)(memmap_addr_saved + *cursor);
    out->base = entry->addr;
    out->length = entry->len;
    out->type = entry->type;
    *cursor += entry->size + sizeof(entry->size);
    return 1;
}
