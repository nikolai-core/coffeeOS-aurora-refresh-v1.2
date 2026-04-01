#ifndef PCI_H
#define PCI_H

#include <stdint.h>

#define PCI_CONFIG_ADDRESS 0xCF8u
#define PCI_CONFIG_DATA    0xCFCu

typedef struct PciDevice {
    uint8_t  bus;
    uint8_t  dev;
    uint8_t  fn;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint32_t bar[6];
    uint8_t  irq_line;
    int      present;
} PciDevice;

/* Read one 32-bit PCI config register from bus/device/function space. */
uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset);

/* Read one 16-bit PCI config register from bus/device/function space. */
uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset);

/* Write one 32-bit PCI config register in bus/device/function space. */
void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset, uint32_t val);

/* Write one 16-bit PCI config register in bus/device/function space. */
void pci_write16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset, uint16_t val);

/* Find one PCI device by vendor/device ID and fill one output record. */
int pci_find_device(uint16_t vendor, uint16_t device, PciDevice *out);

/* Enumerate all PCI devices and log them to serial. */
int pci_scan_all(void);

#endif
