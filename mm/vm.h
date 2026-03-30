#ifndef VM_H
#define VM_H

#include <stdint.h>

/* lazy virtual allocator for kernel space */

void vm_init(void);

uint32_t allocate_virtual_pages(uint32_t count);
int free_virtual_pages(uint32_t addr, uint32_t count);

/* returns 1 if demand paging / COW handled it */
int vm_handle_page_fault(uint32_t fault_addr, uint32_t err_code, uint32_t eip, uint32_t cs);

#endif
