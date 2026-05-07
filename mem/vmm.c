#include <stdint.h>

#include "pmm.h"
#include "serial.h"
#include "vmm.h"

#include "paging.h"
#include "vm.h"

static inline uint32_t read_cr0(void) {
    uint32_t value;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(value));
    return value;
}

void vmm_init(uint32_t multiboot_info_addr) {
    /*
     * Legacy entry point kept for compatibility with existing kernel code.
     * The real implementation now lives in mm/paging.c + mm/vm.c.
     */
    paging_init(multiboot_info_addr);
    vm_init();
    serial_print("[coffeeOS] vmm_init: bridged to paging/vm\n");
}

void vmm_switch_page_directory(uint32_t page_directory_phys) {
    paging_switch_directory(page_directory_phys);
}

int vmm_map_page(uint32_t virt_addr, uint32_t phys_addr, uint32_t flags) {
    return map_page(virt_addr, phys_addr, flags);
}

int vmm_unmap_page(uint32_t virt_addr) {
    return unmap_page(virt_addr);
}

uint32_t *vmm_current_page_directory(void) {
    return paging_current_directory_identity();
}

uint32_t *vmm_kernel_page_directory(void) {
    return paging_kernel_directory_identity();
}

uint32_t vmm_kernel_page_directory_phys(void) {
    return paging_kernel_directory_phys();
}

uint32_t vmm_create_user_page_directory_phys(void) {
    return paging_create_user_directory_phys();
}

int vmm_map_page_in_pd(uint32_t *page_directory, uint32_t virt_addr, uint32_t phys_addr, uint32_t flags) {
    uint32_t pd_phys = (uint32_t)(uintptr_t)page_directory;
    return paging_map_page_in_directory(pd_phys, virt_addr, phys_addr, flags);
}

int vmm_user_range_accessible(uint32_t *page_directory, uint32_t addr, uint32_t len, int write) {
    uint32_t pd_phys = (uint32_t)(uintptr_t)page_directory;
    uint32_t end;
    uint32_t a;

    if (len == 0u) {
        return 1;
    }
    if (addr >= 0xC0000000u) {
        return 0;
    }

    end = addr + len - 1u;
    if (end < addr || end >= 0xC0000000u) {
        return 0;
    }
    a = addr & 0xFFFFF000u;

    while (1) {
        uint32_t phys;
        uint32_t flags;

        if (!paging_get_mapping_in_directory(pd_phys, a, &phys, &flags)) {
            return 0;
        }
        (void)phys;

        if ((flags & VMM_FLAG_USER) == 0u) {
            return 0;
        }
        if (write && ((flags & VMM_FLAG_WRITE) == 0u)) {
            return 0;
        }

        if (a >= (end & 0xFFFFF000u)) {
            break;
        }
        a += PAGE_SIZE;
    }

    return 1;
}

uint32_t vmm_get_cr3(void) {
    return paging_current_directory_phys();
}

int vmm_is_paging_enabled(void) {
    return ((read_cr0() >> 31) & 1u) != 0u;
}
