#ifndef PIT_H
#define PIT_H

#include <stdint.h>

void pit_init(uint32_t hz);
void pit_handle_irq(void);
uint32_t get_ticks(void);

#endif
