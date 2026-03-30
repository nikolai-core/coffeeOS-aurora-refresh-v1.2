#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

/* classic i386 2-level paging, NX is just a placeholder flag here */

#define PAGE_SIZE 4096u

#define KERNEL_VIRT_BASE 0xC0000000u

enum {
    PAGING_FLAG_PRESENT  = 1u << 0,
    PAGING_FLAG_WRITABLE = 1u << 1,
    PAGING_FLAG_USER     = 1u << 2,

    /* reserved for later */
    PAGING_FLAG_NX       = 1u << 3,

    /* software bit for COW */
    PAGING_FLAG_COW      = 1u << 9
};

void paging_init(uint32_t multiboot_info_addr);

int map_page(uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags);
int unmap_page(uint32_t virtual_addr);
uint32_t translate_virtual_address(uint32_t virtual_addr);

uint32_t paging_current_directory_phys(void);
uint32_t paging_kernel_directory_phys(void);
uint32_t *paging_current_directory_identity(void);
uint32_t *paging_kernel_directory_identity(void);

void paging_switch_directory(uint32_t page_directory_phys);
uint32_t paging_create_user_directory_phys(void);
int paging_map_page_in_directory(uint32_t page_directory_phys, uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags);
int paging_unmap_page_in_directory(uint32_t page_directory_phys, uint32_t virtual_addr);

int paging_get_mapping(uint32_t virtual_addr, uint32_t *out_physical_addr, uint32_t *out_pte_flags);
int paging_get_mapping_in_directory(uint32_t page_directory_phys, uint32_t virtual_addr, uint32_t *out_physical_addr, uint32_t *out_pte_flags);

/* only touches WRITABLE/USER/COW, PRESENT stays set */
int paging_update_page_flags(uint32_t virtual_addr, uint32_t set_flags, uint32_t clear_flags);
int paging_update_page_flags_in_directory(uint32_t page_directory_phys, uint32_t virtual_addr, uint32_t set_flags, uint32_t clear_flags);

/* single scratch mapping, invalidated by the next call */
void *paging_temp_map(uint32_t physical_addr);

#endif
