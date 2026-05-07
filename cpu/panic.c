#include <stdint.h>

#include "gfx.h"
#include "panic.h"
#include "process.h"
#include "serial.h"

static const char *exception_names[32] = {
    "Divide Error", "Debug Exception", "NMI Interrupt", "Breakpoint",
    "Overflow", "BOUND Range Exceeded", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS", "Segment Not Present",
    "Stack Fault", "General Protection", "Page Fault", "Reserved",
    "x87 FPU Error", "Alignment Check", "Machine Check", "SIMD FP Exception",
    "Virtualization Exception", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Security Exception", "Reserved", "Reserved"
};

static const char *state_names[] = {
    "UNUSED", "READY", "RUNNING", "EXITED", "FAULTED"
};

static void print_hex(uint32_t val) {
    serial_print("0x");
    serial_write_hex(val);
}

static void gfx_print_hex(uint32_t val) {
    gfx_print("0x");
    gfx_write_hex(val);
}

static void dump_registers(const struct InterruptFrame *frame) {
    serial_print("=== REGISTERS ===\n");
    serial_print("EAX: "); serial_write_hex(frame->eax); serial_print("  ");
    serial_print("EBX: "); serial_write_hex(frame->ebx); serial_print("  ");
    serial_print("ECX: "); serial_write_hex(frame->ecx); serial_print("  ");
    serial_print("EDX: "); serial_write_hex(frame->edx); serial_print("\n");
    serial_print("ESI: "); serial_write_hex(frame->esi); serial_print("  ");
    serial_print("EDI: "); serial_write_hex(frame->edi); serial_print("  ");
    serial_print("EBP: "); serial_write_hex(frame->ebp); serial_print("  ");
    serial_print("ESP: "); serial_write_hex(frame->esp); serial_print("\n");
    serial_print("CS: "); serial_write_hex(frame->cs); serial_print("  ");
    serial_print("EIP: "); serial_write_hex(frame->eip); serial_print("  ");
    serial_print("EFLAGS: "); serial_write_hex(frame->eflags); serial_print("\n");
    serial_print("DS: "); serial_write_hex(frame->ds); serial_print("  ");
    serial_print("ES: "); serial_write_hex(frame->es); serial_print("  ");
    serial_print("FS: "); serial_write_hex(frame->fs); serial_print("  ");
    serial_print("GS: "); serial_write_hex(frame->gs); serial_print("\n");
    serial_print("UserESP: "); serial_write_hex(frame->useresp); serial_print("  ");
    serial_print("SS: "); serial_write_hex(frame->ss); serial_print("\n");
}

static void dump_process_info(void) {
    struct Process *proc = process_current();
    uint32_t pid = process_current_pid();

    serial_print("=== PROCESS INFO ===\n");
    serial_print("Current PID: ");
    serial_write_hex(pid);
    serial_print("\n");

    if (proc != (struct Process *)0) {
        serial_print("Process State: ");
        if (proc->state < 5) {
            serial_print(state_names[proc->state]);
        } else {
            serial_print("UNKNOWN");
        }
        serial_print("\n");
        serial_print("Exit Code: ");
        serial_write_hex(proc->exit_code);
        serial_print("\n");
        serial_print("Page Dir Phys: ");
        serial_write_hex(proc->page_directory_phys);
        serial_print("\n");
        serial_print("User Entry: ");
        serial_write_hex(proc->user_entry);
        serial_print("\n");
        serial_print("User Stack: ");
        serial_write_hex(proc->user_stack_top);
        serial_print("\n");
        serial_print("Kernel Stack: ");
        serial_write_hex(proc->kernel_stack_top);
        serial_print("\n");
    } else {
        serial_print("No active process (kernel mode)\n");
    }
}

static void dump_stack_trace(uint32_t ebp, uint32_t eip) {
    serial_print("=== STACK TRACE ===\n");
    serial_print("EIP: ");
    serial_write_hex(eip);
    serial_print("\n");

    if (ebp != 0u) {
        serial_print("EBP chain:\n");
        uint32_t count = 0;
        uint32_t *frame = (uint32_t *)ebp;

        while (frame != (uint32_t *)0 && count < 16u) {
            serial_print("  [");
            serial_write_hex((uint32_t)frame);
            serial_print("] -> ");
            serial_write_hex(frame[1]);
            serial_print("\n");
            frame = (uint32_t *)*frame;
            count++;
        }
        if (count >= 16u) {
            serial_print("  (truncated)\n");
        }
    }
}

static void dump_page_fault_info(uint32_t cr2, uint32_t err_code) {
    serial_print("=== PAGE FAULT DETAILS ===\n");
    serial_print("CR2 (fault addr): ");
    serial_write_hex(cr2);
    serial_print("\n");
    serial_print("Error Code: ");
    serial_write_hex(err_code);
    serial_print("\n");
    serial_print("  Present: ");
    serial_write_hex(err_code & 1u);
    serial_print("\n");
    serial_print("  Write: ");
    serial_write_hex((err_code >> 1) & 1u);
    serial_print("\n");
    serial_print("  User: ");
    serial_write_hex((err_code >> 2) & 1u);
    serial_print("\n");
    serial_print("  Reserved: ");
    serial_write_hex((err_code >> 3) & 1u);
    serial_print("\n");
    serial_print("  InstructionFetch: ");
    serial_write_hex((err_code >> 4) & 1u);
    serial_print("\n");
}

static void gfx_dump(const struct InterruptFrame *frame, uint32_t cr2, int is_page_fault) {
    gfx_print("=== KERNEL PANIC ===\n");
    gfx_print("Exception: ");
    if (frame->int_no < 32u) {
        gfx_print_hex(frame->int_no);
        gfx_print(" (");
        if (frame->int_no < 32 && exception_names[frame->int_no] != 0) {
            gfx_print(exception_names[frame->int_no]);
        }
        gfx_print(")\n");
    } else {
        gfx_print_hex(frame->int_no);
        gfx_print("\n");
    }

    if (is_page_fault) {
        gfx_print("CR2: ");
        gfx_print_hex(cr2);
        gfx_print("\n");
    }

    gfx_print("EIP: ");
    gfx_print_hex(frame->eip);
    gfx_print("\n");

    uint32_t pid = process_current_pid();
    gfx_print("PID: ");
    gfx_print_hex(pid);
    gfx_print("\n");

    gfx_print("System halted.");
}

void panic_dump(struct InterruptFrame *frame, uint32_t cr2) {
    uint32_t int_no = frame->int_no;
    int is_page_fault = (int_no == 14u);

    serial_print("\n");
    serial_print("********************************\n");
    serial_print("*       KERNEL PANIC           *\n");
    serial_print("********************************\n");
    serial_print("Exception: ");
    serial_write_hex(int_no);
    if (int_no < 32u && exception_names[int_no] != 0) {
        serial_print(" (");
        serial_print(exception_names[int_no]);
        serial_print(")");
    }
    serial_print("\n");
    serial_print("Error Code: ");
    serial_write_hex(frame->err_code);
    serial_print("\n");

    dump_process_info();

    if (is_page_fault) {
        dump_page_fault_info(cr2, frame->err_code);
    }

    dump_registers(frame);
    dump_stack_trace(frame->ebp, frame->eip);

    serial_print("********************************\n");
    serial_print("System halted.\n");

    gfx_dump(frame, cr2, is_page_fault);
}

void panic_halt(const char *reason) {
    serial_print("\n");
    serial_print("********************************\n");
    serial_print("*       KERNEL PANIC           *\n");
    serial_print("********************************\n");
    serial_print("Reason: ");
    serial_print(reason);
    serial_print("\n");

    gfx_print("=== KERNEL PANIC ===\n");
    gfx_print("Reason: ");
    gfx_print(reason);
    gfx_print("\nSystem halted.");

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}