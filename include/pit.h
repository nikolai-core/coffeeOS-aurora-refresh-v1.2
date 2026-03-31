#ifndef PIT_H
#define PIT_H

#include <stdint.h>

void pit_init(uint32_t hz);
void pit_handle_irq(void);
uint32_t get_ticks(void);
int pit_take_fs_sync_request(void);
void pit_request_fs_sync(void);

#endif
