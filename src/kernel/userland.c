#include <stdint.h>

#include "elf32.h"
#include "gfx.h"
#include "multiboot.h"
#include "pmm.h"
#include "process.h"
#include "serial.h"
#include "userland.h"
#include "vmm.h"
#include "paging.h"

#define PAGE_SIZE 4096u
#define USER_STACK_TOP 0x00800000u
#define USER_STACK_PAGES 4u

#define PF_X 1u
#define PF_W 2u

static uint32_t userland_boot_info_addr;
static uint32_t userland_kernel_page_directory_phys;
static int userland_running;

static void memcopy(void *dst, const void *src, uint32_t len) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    uint32_t i;
    for (i = 0; i < len; i++) {
        d[i] = s[i];
    }
}

static void memzero(void *dst, uint32_t len) {
    uint8_t *d = (uint8_t *)dst;
    uint32_t i;
    for (i = 0; i < len; i++) {
        d[i] = 0;
    }
}

static int elf_validate(const struct Elf32_Ehdr *eh) {
    if (eh->e_ident[0] != ELF_MAGIC0
        || eh->e_ident[1] != (uint8_t)ELF_MAGIC1
        || eh->e_ident[2] != (uint8_t)ELF_MAGIC2
        || eh->e_ident[3] != (uint8_t)ELF_MAGIC3) {
        return 0;
    }
    if (eh->e_ident[4] != ELFCLASS32) {
        return 0;
    }
    if (eh->e_ident[5] != ELFDATA2LSB) {
        return 0;
    }
    if (eh->e_type != ET_EXEC) {
        return 0;
    }
    if (eh->e_machine != EM_386) {
        return 0;
    }
    if (eh->e_phentsize != sizeof(struct Elf32_Phdr)) {
        return 0;
    }
    return 1;
}

static int map_user_range(uint32_t *pd, uint32_t start, uint32_t size, uint32_t flags) {
    uint32_t addr;
    uint32_t end = start + size;

    start &= 0xFFFFF000u;
    end = (end + PAGE_SIZE - 1u) & 0xFFFFF000u;

    for (addr = start; addr < end; addr += PAGE_SIZE) {
        void *frame = pmm_alloc_page();
        if (frame == (void *)0) {
            return 0;
        }
        if (!vmm_map_page_in_pd(pd, addr, (uint32_t)(uintptr_t)frame, flags | VMM_FLAG_PRESENT)) {
            return 0;
        }
    }
    return 1;
}

static void clear_user_write_range(uint32_t start, uint32_t size) {
    uint32_t addr;
    uint32_t end = start + size;

    start &= 0xFFFFF000u;
    end = (end + PAGE_SIZE - 1u) & 0xFFFFF000u;

    for (addr = start; addr < end; addr += PAGE_SIZE) {
        (void)paging_update_page_flags(addr, 0u, PAGING_FLAG_WRITABLE);
    }
}

extern int userland_enter(uint32_t entry, uint32_t user_stack_top,
                          struct UserlandResumeContext *resume_context);
extern void userland_resume(uint32_t exit_code,
                            const struct UserlandResumeContext *resume_context);

void userland_set_boot_info_addr(uint32_t multiboot_info_addr) {
    userland_boot_info_addr = multiboot_info_addr;
}

int userland_active(void) {
    return userland_running;
}

void userland_return_from_syscall(uint32_t exit_code) {
    struct Process *process = process_current();

    if (!userland_running || process == (struct Process *)0) {
        gfx_print("\n[user] exit requested with no active userland session\n");
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }

    userland_running = 0;
    process_mark_exited(process, exit_code);
    process_switch_to_kernel();
    __asm__ volatile ("sti");
    userland_resume(exit_code, &process->resume_context);

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

void userland_fault_current_process(uint32_t fault_code) {
    struct Process *process = process_current();

    if (process == (struct Process *)0) {
        return;
    }

    userland_running = 0;
    process_mark_faulted(process, fault_code);
    process_switch_to_kernel();
    __asm__ volatile ("sti");
    userland_resume(fault_code, &process->resume_context);

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

void userland_start(uint32_t multiboot_info_addr) {
    struct MultibootInfo *mbi;
    struct MultibootModule *mods;
    const uint8_t *image;
    uint32_t image_size;
    const struct Elf32_Ehdr *eh;
    uint32_t user_pd_phys;
    uint32_t *user_pd;
    uint32_t entry;
    uint32_t i;
    int exit_code;
    struct Process *process;

    if (multiboot_info_addr != 0u) {
        userland_boot_info_addr = multiboot_info_addr;
    }

    mbi = (struct MultibootInfo *)(uintptr_t)(KERNEL_VIRT_BASE + userland_boot_info_addr);

    if ((mbi->flags & MULTIBOOT_INFO_MODS) == 0u || mbi->mods_count == 0u) {
        gfx_print("No userland module provided.\n");
        serial_print("[coffeeOS] userland: no modules\n");
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }

    mods = (struct MultibootModule *)(uintptr_t)(KERNEL_VIRT_BASE + mbi->mods_addr);
    image = (const uint8_t *)(uintptr_t)(KERNEL_VIRT_BASE + mods[0].mod_start);
    image_size = mods[0].mod_end - mods[0].mod_start;
    eh = (const struct Elf32_Ehdr *)image;

    if (image_size < sizeof(*eh) || !elf_validate(eh)) {
        gfx_print("Invalid userland ELF module.\n");
        serial_print("[coffeeOS] userland: invalid elf\n");
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }

    user_pd_phys = vmm_create_user_page_directory_phys();
    if (user_pd_phys == 0u) {
        gfx_print("Failed to allocate user address space.\n");
        serial_print("[coffeeOS] userland: OOM pd\n");
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }
    user_pd = (uint32_t *)(uintptr_t)user_pd_phys;

    for (i = 0; i < USER_STACK_PAGES; i++) {
        uint32_t page = USER_STACK_TOP - (i + 1u) * PAGE_SIZE;
        void *frame = pmm_alloc_page();
        if (frame == (void *)0) {
            gfx_print("Failed to allocate user stack.\n");
            serial_print("[coffeeOS] userland: OOM stack\n");
            for (;;) {
                __asm__ volatile ("cli; hlt");
            }
        }
        if (!vmm_map_page_in_pd(user_pd, page, (uint32_t)(uintptr_t)frame,
                                VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER)) {
            gfx_print("Failed to map user stack page.\n");
            serial_print("[coffeeOS] userland: map stack page failed\n");
            for (;;) {
                __asm__ volatile ("cli; hlt");
            }
        }
    }

    for (i = 0; i < eh->e_phnum; i++) {
        const struct Elf32_Phdr *ph = (const struct Elf32_Phdr *)(image + eh->e_phoff + i * eh->e_phentsize);
        uint32_t flags = VMM_FLAG_USER | VMM_FLAG_WRITE;

        if (ph->p_type != PT_LOAD || ph->p_memsz == 0u) {
            continue;
        }

        if (!map_user_range(user_pd, ph->p_vaddr, ph->p_memsz, flags)) {
            gfx_print("Failed to map userland segment.\n");
            serial_print("[coffeeOS] userland: map fail\n");
            for (;;) {
                __asm__ volatile ("cli; hlt");
            }
        }
    }

    userland_kernel_page_directory_phys = vmm_get_cr3();

    /* copy into user VAs with the target CR3 live */
    vmm_switch_page_directory(user_pd_phys);

    for (i = 0; i < eh->e_phnum; i++) {
        const struct Elf32_Phdr *ph = (const struct Elf32_Phdr *)(image + eh->e_phoff + i * eh->e_phentsize);
        void *dst;
        const void *src;

        if (ph->p_type != PT_LOAD || ph->p_memsz == 0u) {
            continue;
        }

        if (ph->p_offset + ph->p_filesz > image_size) {
            gfx_print("Userland ELF segment out of range.\n");
            serial_print("[coffeeOS] userland: segment OOB\n");
            for (;;) {
                __asm__ volatile ("cli; hlt");
            }
        }

        dst = (void *)(uintptr_t)ph->p_vaddr;
        src = (const void *)(image + ph->p_offset);
        memcopy(dst, src, ph->p_filesz);
        if (ph->p_memsz > ph->p_filesz) {
            memzero((uint8_t *)dst + ph->p_filesz, ph->p_memsz - ph->p_filesz);
        }
    }

    entry = eh->e_entry;

    /* Apply final segment permissions (e.g., text becomes read only). */
    for (i = 0; i < eh->e_phnum; i++) {
        const struct Elf32_Phdr *ph = (const struct Elf32_Phdr *)(image + eh->e_phoff + i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0u) {
            continue;
        }
        if ((ph->p_flags & PF_W) == 0u) {
            clear_user_write_range(ph->p_vaddr, ph->p_memsz);
        }
    }

    process = process_create(user_pd_phys, entry, USER_STACK_TOP);
    if (process == (struct Process *)0) {
        gfx_print("Failed to create process.\n");
        serial_print("[coffeeOS] userland: process table full\n");
        vmm_switch_page_directory(userland_kernel_page_directory_phys);
        return;
    }

    gfx_print("Userland loaded. Entering ring 3 pid=");
    gfx_write_hex(process->pid);
    gfx_print("...\n");
    serial_print("[coffeeOS] userland: entering ring3 pid=");
    serial_write_hex(process->pid);
    serial_print("\n");

    userland_running = 1;
    process_switch_to(process);
    exit_code = userland_enter(process->user_entry, process->user_stack_top, &process->resume_context);
    userland_running = 0;
    process_switch_to_kernel();

    if (process->state == PROCESS_FAULTED) {
        gfx_print("[user] process faulted; returning to desktop\n");
    } else if (exit_code == 1) {
        gfx_print("[user] kernel requested; returning to desktop\n");
    } else {
        gfx_print("[user] exit; returning to desktop\n");
    }
}
