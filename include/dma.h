#ifndef DMA_H
#define DMA_H

#include <stdint.h>

void dma_setup_channel(uint8_t channel, uint32_t addr, uint16_t count, uint8_t mode);
void dma_enable(uint8_t channel);
void dma_disable(uint8_t channel);

#endif
