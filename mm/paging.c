#include <stdint.h>

#include "multiboot.h"
#include "pmm.h"
#include "serial.h"

#include "paging.h"

#define PDE_COUNT 1024u
#define PTE_COUNT 1024u

#define PDE_INDEX(vaddr) (((vaddr) >> 22) & 0x3FFu)
#define PTE_INDEX(vaddr) (((vaddr) >> 12) & 0x3FFu)

#define ADDR_PAGE_ALIGN_MASK 0xFFFFF000u

/* recursive mapping: PD at 0xFFFFF000, PTs at 0xFFC00000 + pd*4096 */
#define RECURSIVE_PD_VADDR 0xFFFFF000u
#define RECURSIVE_PT_BASE  0xFFC00000u

#define PAGING_BOOT_IDENTITY_LIMIT (64u * 1024u * 1024u) /* 64 MiB */

static uint32_t kernel_pd_phys;
static uint32_t current_pd_phys_cache;

extern uint8_t _kernel_text_start;
extern uint8_t _kernel_text_end;

static inline void memzero(void *ptr, uint32_t len) {
    uint8_t *p = (uint8_t *)ptr;
    uint32_t i;
    for (i = 0; i < len; i++) {
        p[i] = 0u;
    }
}

static inline void invlpg(uint32_t addr) {
    __asm__ volatile ("invlpg (%0)" : : "r"(addr) : "memory");
}

static inline uint32_t read_cr0(void) {
    uint32_t value;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(value));
    return value;
}

static inline void write_cr0(uint32_t value) {
    __asm__ volatile ("mov %0, %%cr0" : : "r"(value) : "memory");
}

static inline void write_cr3(uint32_t value) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(value) : "memory");
    current_pd_phys_cache = value & ADDR_PAGE_ALIGN_MASK;
}

static inline uint32_t read_cr3(void) {
    uint32_t value;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(value));
    return value;
}

static uint32_t pte_flags_to_hw(uint32_t flags) {
    uint32_t hw = 0u;
    hw |= (flags & PAGING_FLAG_PRESENT) ? 1u : 0u;
    hw |= (flags & PAGING_FLAG_WRITABLE) ? (1u << 1) : 0u;
    hw |= (flags & PAGING_FLAG_USER) ? (1u << 2) : 0u;
    if ((flags & PAGING_FLAG_COW) != 0u) {
        hw |= PAGING_FLAG_COW;
    }
    return hw;
}

static uint32_t pte_hw_to_flags(uint32_t hw) {
    uint32_t flags = 0u;
    if ((hw & 1u) != 0u) flags |= PAGING_FLAG_PRESENT;
    if ((hw & (1u << 1)) != 0u) flags |= PAGING_FLAG_WRITABLE;
    if ((hw & (1u << 2)) != 0u) flags |= PAGING_FLAG_USER;
    if ((hw & PAGING_FLAG_COW) != 0u) flags |= PAGING_FLAG_COW;
    return flags;
}

static inline uint32_t *current_pd(void) {
    return (uint32_t *)(uintptr_t)RECURSIVE_PD_VADDR;
}

static inline uint32_t *current_pt(uint32_t pd_index) {
    return (uint32_t *)(uintptr_t)(RECURSIVE_PT_BASE + pd_index * PAGE_SIZE);
}

static int ensure_recursive_mapping_current(void);
static uint32_t *walk_get_pte(uint32_t vaddr, int create, uint32_t flags);

void *paging_temp_map(uint32_t physical_addr) {
    /* single scratch mapping at 0xFFBFF000 */
    uint32_t scratch_vaddr = 0xFFBFF000u;
    uint32_t *pte;

    if ((physical_addr & (PAGE_SIZE - 1u)) != 0u) {
        return (void *)0;
    }
    if (!ensure_recursive_mapping_current()) {
        return (void *)0;
    }

    pte = walk_get_pte(scratch_vaddr, 1, PAGING_FLAG_WRITABLE);
    if (pte == (uint32_t *)0) {
        return (void *)0;
    }

    *pte = (physical_addr & ADDR_PAGE_ALIGN_MASK) | 1u | (1u << 1);
    invlpg(scratch_vaddr);
    return (void *)(uintptr_t)scratch_vaddr;
}

static int ensure_recursive_mapping_current(void) {
    uint32_t *pd = current_pd();
    if ((pd[1023u] & 1u) == 0u) {
        return 0;
    }
    return 1;
}

static uint32_t *walk_get_pte(uint32_t vaddr, int create, uint32_t flags) {
    uint32_t pd_i = PDE_INDEX(vaddr);
    uint32_t pt_i = PTE_INDEX(vaddr);
    uint32_t *pd = current_pd();
    uint32_t pde = pd[pd_i];

    if ((pde & 1u) == 0u) {
        uint32_t pt_phys;

        if (!create) {
            return (uint32_t *)0;
        }

        pt_phys = pmm_alloc_page_below(PAGING_BOOT_IDENTITY_LIMIT);
        if (pt_phys == 0u) {
            pt_phys = pmm_alloc_page_phys();
        }
        if (pt_phys == 0u) {
            return (uint32_t *)0;
        }

        pd[pd_i] = (pt_phys & ADDR_PAGE_ALIGN_MASK)
            | 1u /* present */
            | (1u << 1) /* writable so we can populate PTEs */
            | ((flags & PAGING_FLAG_USER) ? (1u << 2) : 0u);

        invlpg(RECURSIVE_PT_BASE + pd_i * PAGE_SIZE);
        memzero(current_pt(pd_i), PAGE_SIZE);
        pde = pd[pd_i];
    } else {
        if ((flags & PAGING_FLAG_USER) != 0u) {
            if ((pde & (1u << 2)) == 0u) {
                pd[pd_i] = pde | (1u << 2);
            }
        }
    }

    (void)pde;
    return &current_pt(pd_i)[pt_i];
}

uint32_t paging_current_directory_phys(void) {
    if (current_pd_phys_cache == 0u) {
        current_pd_phys_cache = read_cr3() & ADDR_PAGE_ALIGN_MASK;
    }
    return current_pd_phys_cache;
}

uint32_t paging_kernel_directory_phys(void) {
    return kernel_pd_phys;
}

uint32_t *paging_current_directory_identity(void) {
    return (uint32_t *)(uintptr_t)paging_current_directory_phys();
}

uint32_t *paging_kernel_directory_identity(void) {
    return (uint32_t *)(uintptr_t)kernel_pd_phys;
}

void paging_switch_directory(uint32_t page_directory_phys) {
    write_cr3(page_directory_phys & ADDR_PAGE_ALIGN_MASK);
}

static int map_page_current(uint32_t vaddr, uint32_t paddr, uint32_t flags) {
    uint32_t *pte;

    if ((flags & PAGING_FLAG_NX) != 0u) {
        return 0;
    }
    if (((vaddr | paddr) & (PAGE_SIZE - 1u)) != 0u) {
        return 0;
    }
    if ((flags & PAGING_FLAG_PRESENT) == 0u) {
        return 0;
    }
    if (!ensure_recursive_mapping_current()) {
        return 0;
    }

    pte = walk_get_pte(vaddr, 1, flags);
    if (pte == (uint32_t *)0) {
        return 0;
    }
    if (((*pte) & 1u) != 0u) {
        return 0;
    }

    *pte = (paddr & ADDR_PAGE_ALIGN_MASK) | pte_flags_to_hw(flags);
    invlpg(vaddr);
    return 1;
}

int map_page(uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags) {
    return map_page_current(virtual_addr, physical_addr, flags);
}

static int unmap_page_current(uint32_t vaddr) {
    uint32_t *pte;

    if ((vaddr & (PAGE_SIZE - 1u)) != 0u) {
        return 0;
    }
    if (!ensure_recursive_mapping_current()) {
        return 0;
    }

    pte = walk_get_pte(vaddr, 0, 0u);
    if (pte == (uint32_t *)0) {
        return 0;
    }
    if (((*pte) & 1u) == 0u) {
        return 0;
    }

    *pte = 0u;
    invlpg(vaddr);
    return 1;
}

int unmap_page(uint32_t virtual_addr) {
    return unmap_page_current(virtual_addr);
}

uint32_t translate_virtual_address(uint32_t virtual_addr) {
    uint32_t phys;
    if (!paging_get_mapping(virtual_addr, &phys, (uint32_t *)0)) {
        return 0u;
    }
    return phys;
}

int paging_get_mapping(uint32_t virtual_addr, uint32_t *out_physical_addr, uint32_t *out_pte_flags) {
    uint32_t vpage = virtual_addr & ADDR_PAGE_ALIGN_MASK;
    uint32_t offset = virtual_addr & (PAGE_SIZE - 1u);
    uint32_t *pte;
    uint32_t hw;

    if (!ensure_recursive_mapping_current()) {
        return 0;
    }

    pte = walk_get_pte(vpage, 0, 0u);
    if (pte == (uint32_t *)0) {
        return 0;
    }

    hw = *pte;
    if ((hw & 1u) == 0u) {
        return 0;
    }

    if (out_physical_addr != (uint32_t *)0) {
        *out_physical_addr = (hw & ADDR_PAGE_ALIGN_MASK) + offset;
    }
    if (out_pte_flags != (uint32_t *)0) {
        *out_pte_flags = pte_hw_to_flags(hw);
    }
    return 1;
}

int paging_update_page_flags(uint32_t virtual_addr, uint32_t set_flags, uint32_t clear_flags) {
    uint32_t vpage = virtual_addr & ADDR_PAGE_ALIGN_MASK;
    uint32_t *pte;
    uint32_t hw;
    uint32_t new_hw;

    if ((virtual_addr & (PAGE_SIZE - 1u)) != 0u) {
        return 0;
    }
    if (!ensure_recursive_mapping_current()) {
        return 0;
    }

    pte = walk_get_pte(vpage, 0, 0u);
    if (pte == (uint32_t *)0) {
        return 0;
    }
    hw = *pte;
    if ((hw & 1u) == 0u) {
        return 0;
    }

    new_hw = hw;

    if ((set_flags & PAGING_FLAG_WRITABLE) != 0u) new_hw |= (1u << 1);
    if ((clear_flags & PAGING_FLAG_WRITABLE) != 0u) new_hw &= ~(1u << 1);

    if ((set_flags & PAGING_FLAG_USER) != 0u) new_hw |= (1u << 2);
    if ((clear_flags & PAGING_FLAG_USER) != 0u) new_hw &= ~(1u << 2);

    if ((set_flags & PAGING_FLAG_COW) != 0u) new_hw |= PAGING_FLAG_COW;
    if ((clear_flags & PAGING_FLAG_COW) != 0u) new_hw &= ~PAGING_FLAG_COW;

    *pte = new_hw;
    invlpg(vpage);
    return 1;
}

int paging_update_page_flags_in_directory(uint32_t page_directory_phys, uint32_t virtual_addr, uint32_t set_flags, uint32_t clear_flags) {
    uint32_t saved = paging_current_directory_phys();
    int ok;

    paging_switch_directory(page_directory_phys);
    ok = paging_update_page_flags(virtual_addr, set_flags, clear_flags);
    paging_switch_directory(saved);

    return ok;
}

int paging_map_page_in_directory(uint32_t page_directory_phys, uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags) {
    uint32_t saved = paging_current_directory_phys();
    int ok;

    paging_switch_directory(page_directory_phys);
    ok = map_page_current(virtual_addr, physical_addr, flags);
    paging_switch_directory(saved);

    return ok;
}

int paging_unmap_page_in_directory(uint32_t page_directory_phys, uint32_t virtual_addr) {
    uint32_t saved = paging_current_directory_phys();
    int ok;

    paging_switch_directory(page_directory_phys);
    ok = unmap_page_current(virtual_addr);
    paging_switch_directory(saved);

    return ok;
}

int paging_get_mapping_in_directory(uint32_t page_directory_phys, uint32_t virtual_addr, uint32_t *out_physical_addr, uint32_t *out_pte_flags) {
    uint32_t saved = paging_current_directory_phys();
    int ok;

    paging_switch_directory(page_directory_phys);
    ok = paging_get_mapping(virtual_addr, out_physical_addr, out_pte_flags);
    paging_switch_directory(saved);

    return ok;
}

uint32_t paging_create_user_directory_phys(void) {
    uint32_t pd_phys = pmm_alloc_page_below(PAGING_BOOT_IDENTITY_LIMIT);
    uint32_t *pd;
    uint32_t i;

    if (pd_phys == 0u) {
        pd_phys = pmm_alloc_page_phys();
        if (pd_phys == 0u) {
            return 0u;
        }
    }

    pd = (uint32_t *)(uintptr_t)pd_phys;
    memzero(pd, PAGE_SIZE);

    /* copy kernel half, keep it supervisor-only */
    for (i = 768u; i < 1023u; i++) {
        pd[i] = paging_kernel_directory_identity()[i] & ~((uint32_t)PAGING_FLAG_USER);
    }

    pd[1023u] = (pd_phys & ADDR_PAGE_ALIGN_MASK) | 1u | (1u << 1);

    return pd_phys;
}

static void serial_print_u32(const char *label, uint32_t value) {
    serial_print(label);
    serial_write_hex(value);
    serial_print("\n");
}

static void map_identity_range(uint32_t start, uint32_t end, uint32_t flags) {
    uint32_t addr;
    start &= ADDR_PAGE_ALIGN_MASK;
    end = (end + PAGE_SIZE - 1u) & ADDR_PAGE_ALIGN_MASK;
    for (addr = start; addr < end; addr += PAGE_SIZE) {
        (void)map_page(addr, addr, flags | PAGING_FLAG_PRESENT);
    }
}

static void map_physical_range_identity(uint32_t start, uint32_t end, uint32_t flags) {
    map_identity_range(start, end, flags);
}

static void map_physical_range_kernel_window(uint32_t start, uint32_t end, uint32_t flags) {
    uint32_t max_phys = 0xFFFFFFFFu - KERNEL_VIRT_BASE;
    uint32_t addr;

    start &= ADDR_PAGE_ALIGN_MASK;
    end = (end + PAGE_SIZE - 1u) & ADDR_PAGE_ALIGN_MASK;

    if (start > max_phys) {
        return;
    }
    if (end > max_phys + PAGE_SIZE) {
        end = max_phys + PAGE_SIZE;
    }

    for (addr = start; addr < end; addr += PAGE_SIZE) {
        uint32_t vaddr = KERNEL_VIRT_BASE + addr;
        (void)map_page(vaddr, addr, flags | PAGING_FLAG_PRESENT);
    }
}

static void protect_kernel_text_readonly(void) {
    uint32_t start = (uint32_t)(uintptr_t)&_kernel_text_start;
    uint32_t end = (uint32_t)(uintptr_t)&_kernel_text_end;
    uint32_t addr;

    start &= ADDR_PAGE_ALIGN_MASK;
    end = (end + PAGE_SIZE - 1u) & ADDR_PAGE_ALIGN_MASK;

    for (addr = start; addr < end; addr += PAGE_SIZE) {
        uint32_t *pte = walk_get_pte(addr, 0, 0u);
        if (pte == (uint32_t *)0) {
            continue;
        }
        if (((*pte) & 1u) == 0u) {
            continue;
        }
        *pte &= ~(1u << 1);
        invlpg(addr);
    }
}

void paging_init(uint32_t multiboot_info_addr) {
    struct MultibootInfo *mbi = (struct MultibootInfo *)(uintptr_t)multiboot_info_addr;
    uint32_t cr0;
    uint32_t pd_phys;
    uint32_t *pd;
    uint32_t i;
    uint32_t identity_tables = PAGING_BOOT_IDENTITY_LIMIT / (4u * 1024u * 1024u);

    if (kernel_pd_phys != 0u) {
        return;
    }

    if (identity_tables == 0u) {
        identity_tables = 1u;
    }
    if (identity_tables > 64u) {
        identity_tables = 64u;
    }

    serial_print("[coffeeOS] paging_init...\n");
    serial_print_u32("[coffeeOS] cr3(before)=", paging_current_directory_phys());

    pd_phys = pmm_alloc_page_below(PAGING_BOOT_IDENTITY_LIMIT);
    if (pd_phys == 0u) {
        serial_print("[coffeeOS] paging_init: OOM pd\n");
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }
    pd = (uint32_t *)(uintptr_t)pd_phys;
    memzero(pd, PAGE_SIZE);

    for (i = 0; i < identity_tables; i++) {
        uint32_t pt_phys = pmm_alloc_page_below(PAGING_BOOT_IDENTITY_LIMIT);
        uint32_t *pt;
        uint32_t p;

        if (pt_phys == 0u) {
            serial_print("[coffeeOS] paging_init: OOM pt\n");
            for (;;) {
                __asm__ volatile ("cli; hlt");
            }
        }

        pt = (uint32_t *)(uintptr_t)pt_phys;
        for (p = 0; p < PTE_COUNT; p++) {
            uint32_t phys = (i * (4u * 1024u * 1024u)) + (p * PAGE_SIZE);
            pt[p] = (phys & ADDR_PAGE_ALIGN_MASK) | 1u | (1u << 1);
        }

        pd[i] = (pt_phys & ADDR_PAGE_ALIGN_MASK) | 1u | (1u << 1);
        pd[768u + i] = (pt_phys & ADDR_PAGE_ALIGN_MASK) | 1u | (1u << 1);
    }

    pd[1023u] = (pd_phys & ADDR_PAGE_ALIGN_MASK) | 1u | (1u << 1);

    kernel_pd_phys = pd_phys;

    /* paging is already on; CR3 reload gets us recursive mappings */
    paging_switch_directory(kernel_pd_phys);
    cr0 = read_cr0();
    cr0 |= (1u << 16);
    cr0 |= (1u << 31);
    write_cr0(cr0);

    serial_print_u32("[coffeeOS] cr3(after)=", paging_current_directory_phys());

    protect_kernel_text_readonly();

    /* multiboot pointers are still treated as physical addresses here */
    map_physical_range_identity(multiboot_info_addr, multiboot_info_addr + (uint32_t)sizeof(*mbi),
                                PAGING_FLAG_WRITABLE);
    map_physical_range_kernel_window(multiboot_info_addr, multiboot_info_addr + (uint32_t)sizeof(*mbi),
                                     PAGING_FLAG_WRITABLE);

    if ((mbi->flags & MULTIBOOT_INFO_MEM_MAP) != 0u) {
        map_physical_range_identity(mbi->mmap_addr, mbi->mmap_addr + mbi->mmap_length, PAGING_FLAG_WRITABLE);
        map_physical_range_kernel_window(mbi->mmap_addr, mbi->mmap_addr + mbi->mmap_length, PAGING_FLAG_WRITABLE);
    }

    if ((mbi->flags & MULTIBOOT_INFO_MODS) != 0u && mbi->mods_count != 0u) {
        uint32_t mods_size = mbi->mods_count * (uint32_t)sizeof(struct MultibootModule);
        uint32_t m;
        map_physical_range_identity(mbi->mods_addr, mbi->mods_addr + mods_size, PAGING_FLAG_WRITABLE);
        map_physical_range_kernel_window(mbi->mods_addr, mbi->mods_addr + mods_size, PAGING_FLAG_WRITABLE);
        for (m = 0; m < mbi->mods_count; m++) {
            struct MultibootModule *mods = (struct MultibootModule *)(uintptr_t)mbi->mods_addr;
            if (mods[m].mod_end > mods[m].mod_start) {
                map_physical_range_identity(mods[m].mod_start, mods[m].mod_end, PAGING_FLAG_WRITABLE);
                map_physical_range_kernel_window(mods[m].mod_start, mods[m].mod_end, PAGING_FLAG_WRITABLE);
            }
            if (mods[m].string != 0u) {
                map_physical_range_identity(mods[m].string, mods[m].string + 256u, PAGING_FLAG_WRITABLE);
                map_physical_range_kernel_window(mods[m].string, mods[m].string + 256u, PAGING_FLAG_WRITABLE);
            }
        }
    }

    if ((mbi->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO) != 0u) {
        uint64_t fb_addr64 = mbi->framebuffer_addr;
        uint64_t fb_size64 = (uint64_t)mbi->framebuffer_pitch * (uint64_t)mbi->framebuffer_height;
        if (fb_addr64 <= 0xFFFFFFFFULL && fb_size64 != 0ULL) {
            uint32_t fb_addr = (uint32_t)fb_addr64;
            uint32_t fb_end = fb_addr + (uint32_t)fb_size64;
            map_physical_range_identity(fb_addr, fb_end, PAGING_FLAG_WRITABLE);
        }
    }

    serial_print("[coffeeOS] paging_init OK\n");
}
