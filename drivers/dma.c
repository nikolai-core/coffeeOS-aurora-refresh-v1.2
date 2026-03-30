#include <stdint.h>

#include "dma.h"
#include "io.h"

static const uint16_t dma_addr_port[8] = {
    0x00u, 0x02u, 0x04u, 0x06u, 0xC0u, 0xC4u, 0xC8u, 0xCCu
};

static const uint16_t dma_count_port[8] = {
    0x01u, 0x03u, 0x05u, 0x07u, 0xC2u, 0xC6u, 0xCAu, 0xCEu
};

static const uint16_t dma_page_port[8] = {
    0x87u, 0x83u, 0x81u, 0x82u, 0x8Fu, 0x8Bu, 0x89u, 0x8Au
};

/* channel 4 is the 16-bit controller cascade */
static uint8_t dma_channel_index(uint8_t channel) {
    return (channel < 4u) ? channel : (uint8_t)(channel - 4u);
}

/* mask one DMA channel while it gets reprogrammed */
void dma_disable(uint8_t channel) {
    if (channel < 4u) {
        io_out8(0x0Au, (uint8_t)(0x04u | channel));
        return;
    }

    io_out8(0xD4u, (uint8_t)(0x04u | dma_channel_index(channel)));
}

/* unmask the channel once address/count/page are stable again */
void dma_enable(uint8_t channel) {
    if (channel < 4u) {
        io_out8(0x0Au, channel);
        return;
    }

    io_out8(0xD4u, dma_channel_index(channel));
}

/* ISA DMA still wants raw page/address/count register programming */
void dma_setup_channel(uint8_t channel, uint32_t addr, uint16_t count, uint8_t mode) {
    uint32_t dma_addr = addr;
    uint16_t dma_count = count;
    uint8_t idx = dma_channel_index(channel);

    if (channel > 7u || channel == 4u) {
        return;
    }

    if (channel >= 4u) {
        dma_addr >>= 1;
        dma_count >>= 1;
    }

    dma_disable(channel);

    if (channel < 4u) {
        io_out8(0x0Cu, 0x00u);
        io_out8(0x0Bu, (uint8_t)(mode | channel));
    } else {
        io_out8(0xD8u, 0x00u);
        io_out8(0xD6u, (uint8_t)(mode | idx));
    }

    io_out8(dma_page_port[channel], (uint8_t)((addr >> 16) & 0xFFu));

    io_out8(dma_addr_port[channel], (uint8_t)(dma_addr & 0xFFu));
    io_out8(dma_addr_port[channel], (uint8_t)((dma_addr >> 8) & 0xFFu));

    io_out8(dma_count_port[channel], (uint8_t)(dma_count & 0xFFu));
    io_out8(dma_count_port[channel], (uint8_t)((dma_count >> 8) & 0xFFu));

    dma_enable(channel);
}
