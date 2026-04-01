#ifndef RTL8139_H
#define RTL8139_H

#include <stdint.h>

#define RTL8139_VENDOR  0x10ECu
#define RTL8139_DEVICE  0x8139u

/* IO register offsets from BAR0. */
#define RTL_MAC0        0x00u
#define RTL_MAR0        0x08u
#define RTL_RBSTART     0x30u
#define RTL_CMD         0x37u
#define RTL_CAPR        0x38u
#define RTL_IMR         0x3Cu
#define RTL_ISR         0x3Eu
#define RTL_TCR         0x40u
#define RTL_RCR         0x44u
#define RTL_CONFIG1     0x52u
#define RTL_TSAD0       0x20u
#define RTL_TSD0        0x10u

/* Command bits. */
#define RTL_CMD_RX_ENABLE 0x08u
#define RTL_CMD_TX_ENABLE 0x04u
#define RTL_CMD_RESET     0x10u
#define RTL_CMD_BUFE      0x01u

/* ISR bits. */
#define RTL_ISR_ROK  0x0001u
#define RTL_ISR_RER  0x0002u
#define RTL_ISR_TOK  0x0004u
#define RTL_ISR_TER  0x0008u

/* Receive config bits. */
#define RTL_RCR_AAP  (1u << 0)
#define RTL_RCR_APM  (1u << 1)
#define RTL_RCR_AM   (1u << 2)
#define RTL_RCR_AB   (1u << 3)
#define RTL_RCR_WRAP (1u << 7)
#define RTL_RCR_RBLEN_32K (1u << 12)

#define RTL_RX_BUF_SIZE    (32768u + 16u + 1500u)
#define RTL_TX_BUF_SIZE    1536u
#define RTL_TX_DESC_COUNT  4u

/* Initialize the RTL8139 and program physical DMA buffers. */
int rtl8139_init(void);

/* Send one raw Ethernet frame payload up to 1500 bytes. */
int rtl8139_send(const void *data, uint16_t len);

/* Drain the RTL8139 RX ring from polling context. */
void rtl8139_poll(void);

/* Acknowledge one RTL8139 interrupt and drain receive state. */
void rtl8139_irq(void);

/* Copy the NIC MAC into one 6-byte output buffer. */
int rtl8139_get_mac(uint8_t *out_mac);

/* Return non-zero when the RTL8139 driver finished initialization. */
int rtl8139_present(void);

/* Return the PIC IRQ line assigned by PCI, or -1 when absent. */
int rtl8139_irq_line(void);

#endif
