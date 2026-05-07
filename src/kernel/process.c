#include <stdint.h>

#include "gdt.h"
#include "process.h"
#include "serial.h"
#include "vmm.h"

#define PROCESS_MAX 8u

static struct Process process_table[PROCESS_MAX];
static struct Process *current_process;
static uint32_t next_pid;
static uint32_t kernel_page_directory_phys;
static uint32_t boot_kernel_stack_top;

static void memzero(void *ptr, uint32_t len) {
    uint8_t *p = (uint8_t *)ptr;
    uint32_t i;

    for (i = 0u; i < len; i++) {
        p[i] = 0u;
    }
}

void process_init(void) {
    uint32_t i;

    for (i = 0u; i < PROCESS_MAX; i++) {
        memzero(&process_table[i], sizeof(process_table[i]));
        process_table[i].state = PROCESS_UNUSED;
    }

    current_process = (struct Process *)0;
    next_pid = 1u;
    kernel_page_directory_phys = vmm_kernel_page_directory_phys();
    __asm__ volatile ("mov %%esp, %0" : "=r"(boot_kernel_stack_top));
}

struct Process *process_create(uint32_t page_directory_phys, uint32_t user_entry, uint32_t user_stack_top) {
    uint32_t i;

    if (page_directory_phys == 0u || user_entry == 0u || user_stack_top == 0u) {
        return (struct Process *)0;
    }

    for (i = 0u; i < PROCESS_MAX; i++) {
        struct Process *process = &process_table[i];

        if (process->state == PROCESS_UNUSED || process->state == PROCESS_EXITED
            || process->state == PROCESS_FAULTED) {
            memzero(process, sizeof(*process));
            process->pid = next_pid++;
            if (next_pid == 0u) {
                next_pid = 1u;
            }
            process->page_directory_phys = page_directory_phys;
            process->user_entry = user_entry;
            process->user_stack_top = user_stack_top;
            process->kernel_stack_top = (uint32_t)(process->kernel_stack + PROCESS_KERNEL_STACK_SIZE);
            process->state = PROCESS_READY;
            return process;
        }
    }

    return (struct Process *)0;
}

struct Process *process_current(void) {
    return current_process;
}

void process_set_current(struct Process *process) {
    current_process = process;
}

void process_switch_to(struct Process *process) {
    if (process == (struct Process *)0) {
        return;
    }

    current_process = process;
    process->state = PROCESS_RUNNING;
    tss_set_kernel_stack(process->kernel_stack_top);
    vmm_switch_page_directory(process->page_directory_phys);
}

void process_switch_to_kernel(void) {
    current_process = (struct Process *)0;
    tss_set_kernel_stack(boot_kernel_stack_top);
    if (kernel_page_directory_phys != 0u) {
        vmm_switch_page_directory(kernel_page_directory_phys);
    }
}

void process_mark_exited(struct Process *process, uint32_t exit_code) {
    if (process == (struct Process *)0) {
        return;
    }

    process->exit_code = exit_code;
    process->state = PROCESS_EXITED;
}

void process_mark_faulted(struct Process *process, uint32_t fault_code) {
    if (process == (struct Process *)0) {
        return;
    }

    process->exit_code = fault_code;
    process->state = PROCESS_FAULTED;
    serial_print("[process] user process faulted pid=");
    serial_write_hex(process->pid);
    serial_print(" code=");
    serial_write_hex(fault_code);
    serial_print("\n");
}

uint32_t process_current_pid(void) {
    return current_process != (struct Process *)0 ? current_process->pid : 0u;
}
