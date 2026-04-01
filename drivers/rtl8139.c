#include <stdint.h>

#include "io.h"
#include "netif.h"
#include "paging.h"
#include "pci.h"
#include "rtl8139.h"
#include "serial.h"

#define PIC1_DATA 0x21u
#define PIC2_DATA 0xA1u

static uint8_t  rtl_rx_buf[RTL_RX_BUF_SIZE] __attribute__((aligned(4)));
static uint8_t  rtl_tx_buf[RTL_TX_DESC_COUNT][RTL_TX_BUF_SIZE] __attribute__((aligned(4)));
static uint32_t rtl_tx_cur;
static uint16_t rtl_rx_ptr;
static uint16_t rtl_io_base;
static uint8_t  rtl_mac[6];
static int      rtl_ready;
static int      rtl_irq_line_value = -1;

/* Return the physical address of one kernel virtual buffer for DMA. */
static uint32_t rtl_phys_addr(const void *ptr) {
    return translate_virtual_address((uint32_t)(uintptr_t)ptr);
}

/* Unmask one PIC IRQ line for the NIC after PCI reports it. */
static void rtl_unmask_irq(uint8_t irq_line) {
    if (irq_line < 8u) {
        io_out8(PIC1_DATA, (uint8_t)(io_in8(PIC1_DATA) & (uint8_t)~(1u << irq_line)));
    } else if (irq_line < 16u) {
        io_out8(PIC1_DATA, (uint8_t)(io_in8(PIC1_DATA) & (uint8_t)~(1u << 2)));
        io_out8(PIC2_DATA, (uint8_t)(io_in8(PIC2_DATA) & (uint8_t)~(1u << (irq_line - 8u))));
    }
}

/* Copy one MAC address out of the card register block. */
static void rtl_read_mac(void) {
    uint32_t i;

    for (i = 0u; i < 6u; i++) {
        rtl_mac[i] = io_in8((uint16_t)(rtl_io_base + RTL_MAC0 + i));
    }
}

/* Drain the RX ring and dispatch every received Ethernet frame. */
void rtl8139_poll(void) {
    uint32_t processed = 0u;

    if (!rtl_ready) {
        return;
    }

    while ((io_in8((uint16_t)(rtl_io_base + RTL_CMD)) & RTL_CMD_BUFE) == 0u && processed < 64u) {
        uint16_t offset = (uint16_t)(rtl_rx_ptr % 32768u);
        uint8_t *entry = rtl_rx_buf + offset;
        uint16_t status = *(uint16_t *)(void *)(entry + 0u);
        uint16_t length = *(uint16_t *)(void *)(entry + 2u);

        if (length == 0u || length > 1600u) {
            break;
        }
        if ((status & 0x0001u) != 0u && length >= 4u) {
            net_receive(entry + 4u, (uint16_t)(length - 4u));
        }

        rtl_rx_ptr = (uint16_t)((rtl_rx_ptr + length + 4u + 3u) & ~3u);
        rtl_rx_ptr %= 32768u;
        io_out16((uint16_t)(rtl_io_base + RTL_CAPR), (uint16_t)(rtl_rx_ptr - 16u));
        processed++;
    }

    io_out16((uint16_t)(rtl_io_base + RTL_ISR), RTL_ISR_ROK);
}

/* Acknowledge one RTL8139 interrupt and drain receive state. */
void rtl8139_irq(void) {
    uint16_t isr;

    if (!rtl_ready) {
        return;
    }
    isr = io_in16((uint16_t)(rtl_io_base + RTL_ISR));
    if (isr == 0u) {
        return;
    }
    io_out16((uint16_t)(rtl_io_base + RTL_ISR), isr);
    rtl8139_poll();
}

/* Initialize the RTL8139 and program physical DMA buffers. */
int rtl8139_init(void) {
    PciDevice pci;
    uint16_t command;
    uint32_t rx_phys;
    uint32_t i;

    rtl_ready = 0;
    rtl_tx_cur = 0u;
    rtl_rx_ptr = 0u;
    rtl_io_base = 0u;
    rtl_irq_line_value = -1;

    (void)pci_scan_all();
    if (pci_find_device(RTL8139_VENDOR, RTL8139_DEVICE, &pci) != 0) {
        serial_print("[rtl8139] device not found\n");
        return -1;
    }

    rtl_io_base = (uint16_t)(pci.bar[0] & ~3u);
    command = pci_read16(pci.bus, pci.dev, pci.fn, 0x04u);
    command |= (1u << 2);
    pci_write16(pci.bus, pci.dev, pci.fn, 0x04u, command);

    io_out8((uint16_t)(rtl_io_base + RTL_CONFIG1), 0x00u);
    io_out8((uint16_t)(rtl_io_base + RTL_CMD), RTL_CMD_RESET);
    for (i = 0u; i < 10000u; i++) {
        if ((io_in8((uint16_t)(rtl_io_base + RTL_CMD)) & RTL_CMD_RESET) == 0u) {
            break;
        }
    }
    if (i == 10000u) {
        serial_print("[rtl8139] reset timeout\n");
        return -1;
    }

    rtl_read_mac();
    rx_phys = rtl_phys_addr(rtl_rx_buf);
    if (rx_phys == 0u) {
        serial_print("[rtl8139] rx buffer has no physical mapping\n");
        return -1;
    }

    io_out32((uint16_t)(rtl_io_base + RTL_RBSTART), rx_phys);
    io_out32((uint16_t)(rtl_io_base + RTL_TCR), 0x03000600u);
    io_out16((uint16_t)(rtl_io_base + RTL_ISR), 0xFFFFu);
    io_out16((uint16_t)(rtl_io_base + RTL_CAPR), 0u);
    io_out16((uint16_t)(rtl_io_base + RTL_IMR), (uint16_t)(RTL_ISR_ROK | RTL_ISR_TOK | RTL_ISR_RER | RTL_ISR_TER));
    io_out32((uint16_t)(rtl_io_base + RTL_RCR),
             RTL_RCR_AAP | RTL_RCR_APM | RTL_RCR_AM | RTL_RCR_AB | RTL_RCR_WRAP | RTL_RCR_RBLEN_32K);
    io_out8((uint16_t)(rtl_io_base + RTL_CMD), (uint8_t)(RTL_CMD_RX_ENABLE | RTL_CMD_TX_ENABLE));

    rtl_irq_line_value = (int)pci.irq_line;
    rtl_unmask_irq((uint8_t)rtl_irq_line_value);
    rtl_ready = 1;
    serial_print("[rtl8139] ready mac=");
    for (i = 0u; i < 6u; i++) {
        serial_write_hex(rtl_mac[i]);
        if (i != 5u) {
            serial_write_char(':');
        }
    }
    serial_print("\n");
    return 0;
}

/* Send one raw Ethernet frame payload up to 1500 bytes. */
int rtl8139_send(const void *data, uint16_t len) {
    uint32_t desc;
    uint32_t status;
    uint32_t phys;
    uint16_t i;

    if (!rtl_ready || data == (const void *)0 || len > 1500u) {
        return -1;
    }

    desc = rtl_tx_cur % RTL_TX_DESC_COUNT;
    for (i = 0u; i < 10000u; i++) {
        status = io_in32((uint16_t)(rtl_io_base + RTL_TSD0 + (desc * 4u)));
        if ((status & (1u << 13)) != 0u) {
            break;
        }
    }
    if (i == 10000u) {
        serial_print("[rtl8139] tx timeout desc=");
        serial_write_hex(desc);
        serial_print(" tsd=");
        serial_write_hex(status);
        serial_print("\n");
        return -1;
    }

    for (i = 0u; i < len; i++) {
        rtl_tx_buf[desc][i] = ((const uint8_t *)data)[i];
    }
    phys = rtl_phys_addr(rtl_tx_buf[desc]);
    if (phys == 0u) {
        serial_print("[rtl8139] tx buffer unmapped desc=");
        serial_write_hex(desc);
        serial_print("\n");
        return -1;
    }

    io_out32((uint16_t)(rtl_io_base + RTL_TSAD0 + (desc * 4u)), phys);
    io_out32((uint16_t)(rtl_io_base + RTL_TSD0 + (desc * 4u)), len & 0x1FFFu);
    rtl_tx_cur++;
    return 0;
}

/* Copy the NIC MAC into one 6-byte output buffer. */
int rtl8139_get_mac(uint8_t *out_mac) {
    uint32_t i;

    if (!rtl_ready || out_mac == (uint8_t *)0) {
        return -1;
    }
    for (i = 0u; i < 6u; i++) {
        out_mac[i] = rtl_mac[i];
    }
    return 0;
}

/* Return non-zero when the RTL8139 driver finished initialization. */
int rtl8139_present(void) {
    return rtl_ready;
}

/* Return the PIC IRQ line assigned by PCI, or -1 when absent. */
int rtl8139_irq_line(void) {
    return rtl_irq_line_value;
}
