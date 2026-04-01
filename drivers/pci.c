#include <stdint.h>

#include "io.h"
#include "pci.h"
#include "serial.h"

/* Emit one small decimal value to serial for PCI scan logs. */
static void pci_write_u32(uint32_t value) {
    char digits[10];
    uint32_t count = 0u;

    if (value == 0u) {
        serial_write_char('0');
        return;
    }
    while (value != 0u && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count != 0u) {
        serial_write_char(digits[--count]);
    }
}

/* Build one PCI config-space address word for the given location. */
static uint32_t pci_config_addr(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset) {
    return 0x80000000u
        | ((uint32_t)bus << 16)
        | ((uint32_t)dev << 11)
        | ((uint32_t)fn << 8)
        | ((uint32_t)offset & 0xFCu);
}

/* Read one 32-bit PCI config register from bus/device/function space. */
uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset) {
    io_out32(PCI_CONFIG_ADDRESS, pci_config_addr(bus, dev, fn, offset));
    return io_in32(PCI_CONFIG_DATA);
}

/* Read one 16-bit PCI config register from bus/device/function space. */
uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset) {
    uint32_t value = pci_read32(bus, dev, fn, offset);
    uint32_t shift = (uint32_t)((offset & 2u) * 8u);

    return (uint16_t)((value >> shift) & 0xFFFFu);
}

/* Write one 32-bit PCI config register in bus/device/function space. */
void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset, uint32_t val) {
    io_out32(PCI_CONFIG_ADDRESS, pci_config_addr(bus, dev, fn, offset));
    io_out32(PCI_CONFIG_DATA, val);
}

/* Write one 16-bit PCI config register in bus/device/function space. */
void pci_write16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset, uint16_t val) {
    uint32_t current = pci_read32(bus, dev, fn, offset);
    uint32_t shift = (uint32_t)((offset & 2u) * 8u);

    current &= ~(0xFFFFu << shift);
    current |= (uint32_t)val << shift;
    pci_write32(bus, dev, fn, offset, current);
}

/* Fill one PciDevice structure from the current config-space location. */
static void pci_fill_device(uint8_t bus, uint8_t dev, uint8_t fn, PciDevice *out) {
    uint32_t class_reg;
    uint32_t i;

    out->bus = bus;
    out->dev = dev;
    out->fn = fn;
    out->vendor_id = pci_read16(bus, dev, fn, 0x00u);
    out->device_id = pci_read16(bus, dev, fn, 0x02u);
    class_reg = pci_read32(bus, dev, fn, 0x08u);
    out->class_code = (uint8_t)((class_reg >> 24) & 0xFFu);
    out->subclass = (uint8_t)((class_reg >> 16) & 0xFFu);
    for (i = 0u; i < 6u; i++) {
        out->bar[i] = pci_read32(bus, dev, fn, (uint8_t)(0x10u + (i * 4u)));
    }
    out->irq_line = (uint8_t)(pci_read32(bus, dev, fn, 0x3Cu) & 0xFFu);
    out->present = 1;
}

/* Find one PCI device by vendor/device ID and fill one output record. */
int pci_find_device(uint16_t vendor, uint16_t device, PciDevice *out) {
    uint32_t bus;
    uint32_t dev;
    uint32_t fn;

    if (out != (PciDevice *)0) {
        out->present = 0;
    }
    for (bus = 0u; bus < 256u; bus++) {
        for (dev = 0u; dev < 32u; dev++) {
            for (fn = 0u; fn < 8u; fn++) {
                uint16_t vendor_id = pci_read16((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 0x00u);

                if (vendor_id == 0xFFFFu) {
                    if (fn == 0u) {
                        break;
                    }
                    continue;
                }
                if (vendor_id == vendor
                    && pci_read16((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 0x02u) == device) {
                    if (out != (PciDevice *)0) {
                        pci_fill_device((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, out);
                    }
                    return 0;
                }
            }
        }
    }
    return -1;
}

/* Enumerate all PCI devices and log them to serial. */
int pci_scan_all(void) {
    uint32_t bus;
    uint32_t dev;
    uint32_t fn;
    int found = 0;

    for (bus = 0u; bus < 256u; bus++) {
        for (dev = 0u; dev < 32u; dev++) {
            for (fn = 0u; fn < 8u; fn++) {
                uint16_t vendor_id = pci_read16((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 0x00u);

                if (vendor_id == 0xFFFFu) {
                    if (fn == 0u) {
                        break;
                    }
                    continue;
                }

                serial_print("[pci] bus=");
                pci_write_u32(bus);
                serial_print(" dev=");
                pci_write_u32(dev);
                serial_print(" fn=");
                pci_write_u32(fn);
                serial_print(" vendor=");
                serial_write_hex(vendor_id);
                serial_print(" device=");
                serial_write_hex(pci_read16((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 0x02u));
                serial_print("\n");
                found++;
            }
        }
    }
    return found;
}
