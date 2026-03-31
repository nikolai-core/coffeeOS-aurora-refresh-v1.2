#ifndef PMM_H
#define PMM_H

#include <stdint.h>

struct PmmMemoryRegion {
    uint64_t base;
    uint64_t length;
    uint32_t type;
};

void pmm_init(uint32_t multiboot_info_addr);
void *pmm_alloc_page(void);
void pmm_free_page(void *addr);
uint32_t pmm_total_pages(void);
uint32_t pmm_used_pages(void);

/* Frame-oriented helpers (physical addresses). */
uint32_t pmm_alloc_page_phys(void);
uint32_t pmm_alloc_page_below(uint32_t max_phys);
void pmm_free_page_phys(uint32_t phys_addr);
void pmm_mark_used(void *addr);

/* Reference counting for shared/COW mappings. */
void pmm_ref_inc(uint32_t phys_addr);
void pmm_ref_dec(uint32_t phys_addr);
uint32_t pmm_ref_count(uint32_t phys_addr);
int pmm_memmap_next(uint32_t *cursor, struct PmmMemoryRegion *out);

#endif
