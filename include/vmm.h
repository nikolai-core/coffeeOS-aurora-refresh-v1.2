#ifndef VMM_H
#define VMM_H

#include <stdint.h>

/* single-address-space i386 paging for now */

enum {
    VMM_FLAG_PRESENT = 1u << 0,
    VMM_FLAG_WRITE = 1u << 1,
    VMM_FLAG_USER = 1u << 2
};

void vmm_init(uint32_t multiboot_info_addr);
void vmm_switch_page_directory(uint32_t page_directory_phys);
int vmm_map_page(uint32_t virt_addr, uint32_t phys_addr, uint32_t flags);
int vmm_unmap_page(uint32_t virt_addr);

uint32_t *vmm_current_page_directory(void);
uint32_t *vmm_kernel_page_directory(void);
uint32_t vmm_kernel_page_directory_phys(void);
uint32_t vmm_create_user_page_directory_phys(void);
int vmm_map_page_in_pd(uint32_t *page_directory, uint32_t virt_addr, uint32_t phys_addr, uint32_t flags);
int vmm_user_range_accessible(uint32_t *page_directory, uint32_t addr, uint32_t len, int write);

uint32_t vmm_get_cr3(void);
int vmm_is_paging_enabled(void);

#endif
