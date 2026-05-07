#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>

#include "userland.h"

#define PROCESS_KERNEL_STACK_SIZE 8192u

enum ProcessState {
    PROCESS_UNUSED = 0,
    PROCESS_READY = 1,
    PROCESS_RUNNING = 2,
    PROCESS_EXITED = 3,
    PROCESS_FAULTED = 4
};

struct Process {
    uint32_t pid;
    uint32_t page_directory_phys;
    uint32_t user_entry;
    uint32_t user_stack_top;
    uint32_t kernel_stack_top;
    uint32_t exit_code;
    enum ProcessState state;
    struct UserlandResumeContext resume_context;
    uint8_t kernel_stack[PROCESS_KERNEL_STACK_SIZE];
};

void process_init(void);
struct Process *process_create(uint32_t page_directory_phys, uint32_t user_entry, uint32_t user_stack_top);
struct Process *process_current(void);
void process_set_current(struct Process *process);
void process_switch_to(struct Process *process);
void process_switch_to_kernel(void);
void process_mark_exited(struct Process *process, uint32_t exit_code);
void process_mark_faulted(struct Process *process, uint32_t fault_code);
uint32_t process_current_pid(void);

#endif
