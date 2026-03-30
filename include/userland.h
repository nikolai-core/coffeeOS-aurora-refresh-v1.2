#ifndef USERLAND_H
#define USERLAND_H

#include <stdint.h>

struct UserlandResumeContext {
    uint32_t kernel_esp;
    uint32_t kernel_ebp;
    uint32_t kernel_ebx;
    uint32_t kernel_esi;
    uint32_t kernel_edi;
};

void userland_set_boot_info_addr(uint32_t multiboot_info_addr);
void userland_start(uint32_t multiboot_info_addr);
int userland_active(void);
void userland_return_from_syscall(uint32_t exit_code);

#endif
