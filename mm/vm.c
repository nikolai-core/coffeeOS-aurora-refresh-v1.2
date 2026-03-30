#include <stdint.h>

#include "pmm.h"
#include "serial.h"

#include "paging.h"
#include "vm.h"

#define VM_KERNEL_HEAP_BASE 0xD0000000u
#define VM_KERNEL_HEAP_SIZE (64u * 1024u * 1024u) /* 64 MiB */
#define VM_KERNEL_HEAP_PAGES (VM_KERNEL_HEAP_SIZE / PAGE_SIZE)

enum {
    VM_PAGE_FREE = 0,
    VM_PAGE_RESERVED = 1,
    VM_PAGE_LAZY = 2
};

static uint8_t vm_page_state[VM_KERNEL_HEAP_PAGES];

static inline uint32_t align_down(uint32_t v, uint32_t a) {
    return v & ~(a - 1u);
}

static inline uint32_t align_up(uint32_t v, uint32_t a) {
    return (v + a - 1u) & ~(a - 1u);
}

static void serial_print_u32(const char *label, uint32_t value) {
    serial_print(label);
    serial_write_hex(value);
    serial_print("\n");
}

void vm_init(void) {
    uint32_t i;
    for (i = 0; i < VM_KERNEL_HEAP_PAGES; i++) {
        vm_page_state[i] = VM_PAGE_FREE;
    }

    serial_print("[coffeeOS] vm_init OK (kernel heap reserved lazy)\n");
    serial_print_u32("[coffeeOS] vm_heap_base=", VM_KERNEL_HEAP_BASE);
    serial_print_u32("[coffeeOS] vm_heap_size=", VM_KERNEL_HEAP_SIZE);
}

uint32_t allocate_virtual_pages(uint32_t count) {
    uint32_t run = 0u;
    uint32_t run_start = 0u;
    uint32_t i;

    if (count == 0u || count > VM_KERNEL_HEAP_PAGES) {
        return 0u;
    }

    for (i = 0; i < VM_KERNEL_HEAP_PAGES; i++) {
        if (vm_page_state[i] == VM_PAGE_FREE) {
            if (run == 0u) {
                run_start = i;
            }
            run++;
            if (run == count) {
                uint32_t j;
                for (j = run_start; j < run_start + count; j++) {
                    vm_page_state[j] = VM_PAGE_LAZY;
                }
                return VM_KERNEL_HEAP_BASE + run_start * PAGE_SIZE;
            }
        } else {
            run = 0u;
        }
    }

    return 0u;
}

int free_virtual_pages(uint32_t addr, uint32_t count) {
    uint32_t start;
    uint32_t end;
    uint32_t first_page;
    uint32_t last_page;
    uint32_t i;

    if (count == 0u) {
        return 1;
    }
    if ((addr & (PAGE_SIZE - 1u)) != 0u) {
        return 0;
    }

    start = addr;
    end = addr + count * PAGE_SIZE;

    if (start < VM_KERNEL_HEAP_BASE || end > (VM_KERNEL_HEAP_BASE + VM_KERNEL_HEAP_SIZE) || end < start) {
        return 0;
    }

    first_page = (start - VM_KERNEL_HEAP_BASE) / PAGE_SIZE;
    last_page = first_page + count;

    for (i = first_page; i < last_page; i++) {
        uint32_t vaddr = VM_KERNEL_HEAP_BASE + i * PAGE_SIZE;
        uint32_t phys_page;
        uint32_t pte_flags;

        if (vm_page_state[i] == VM_PAGE_FREE) {
            return 0;
        }

        if (paging_get_mapping(vaddr, &phys_page, &pte_flags)) {
            uint32_t phys_base = phys_page & 0xFFFFF000u;
            (void)pte_flags;
            (void)unmap_page(vaddr);
            pmm_ref_dec(phys_base);
        }

        vm_page_state[i] = VM_PAGE_FREE;
    }

    return 1;
}

static int vm_heap_contains(uint32_t addr) {
    return addr >= VM_KERNEL_HEAP_BASE && addr < (VM_KERNEL_HEAP_BASE + VM_KERNEL_HEAP_SIZE);
}

static int vm_handle_lazy_fault(uint32_t fault_addr) {
    uint32_t page = align_down(fault_addr, PAGE_SIZE);
    uint32_t idx;
    uint32_t frame;

    if (!vm_heap_contains(page)) {
        return 0;
    }

    idx = (page - VM_KERNEL_HEAP_BASE) / PAGE_SIZE;
    if (idx >= VM_KERNEL_HEAP_PAGES) {
        return 0;
    }

    if (vm_page_state[idx] != VM_PAGE_LAZY) {
        return 0;
    }

    frame = pmm_alloc_page_phys();
    if (frame == 0u) {
        return 0;
    }

    if (!map_page(page, frame, PAGING_FLAG_PRESENT | PAGING_FLAG_WRITABLE)) {
        pmm_ref_dec(frame);
        return 0;
    }

    {
        uint8_t *p = (uint8_t *)(uintptr_t)page;
        uint32_t i;
        for (i = 0; i < PAGE_SIZE; i++) {
            p[i] = 0u;
        }
    }

    vm_page_state[idx] = VM_PAGE_RESERVED;
    return 1;
}

static int vm_handle_cow_fault(uint32_t fault_addr) {
    uint32_t page = align_down(fault_addr, PAGE_SIZE);
    uint32_t phys;
    uint32_t flags;

    if (!paging_get_mapping(page, &phys, &flags)) {
        return 0;
    }

    if ((flags & PAGING_FLAG_COW) == 0u) {
        return 0;
    }

    /* classic COW fault path */
    {
        uint32_t old_phys = phys & 0xFFFFF000u;
        uint32_t new_phys = pmm_alloc_page_phys();
        uint8_t *dst;
        uint8_t *src;
        uint32_t i;

        if (new_phys == 0u) {
            return 0;
        }
        dst = (uint8_t *)paging_temp_map(new_phys);
        if (dst == (uint8_t *)0) {
            pmm_ref_dec(new_phys);
            return 0;
        }
        src = (uint8_t *)(uintptr_t)page;
        for (i = 0; i < PAGE_SIZE; i++) {
            dst[i] = src[i];
        }

        (void)unmap_page(page);
        pmm_ref_dec(old_phys);

        if (!map_page(page, new_phys, PAGING_FLAG_PRESENT | PAGING_FLAG_WRITABLE | (flags & PAGING_FLAG_USER))) {
            pmm_ref_dec(new_phys);
            return 0;
        }
    }

    return 1;
}

int vm_handle_page_fault(uint32_t fault_addr, uint32_t err_code, uint32_t eip, uint32_t cs) {
    uint32_t present = err_code & 1u;
    uint32_t write = (err_code >> 1) & 1u;
    uint32_t user = (err_code >> 2) & 1u;

    (void)eip;
    (void)cs;

    if (!present) {
        if (vm_handle_lazy_fault(fault_addr)) {
            return 1;
        }
        return 0;
    }

    if (present && write) {
        if (vm_handle_cow_fault(fault_addr)) {
            return 1;
        }
    }

    (void)user;
    return 0;
}
