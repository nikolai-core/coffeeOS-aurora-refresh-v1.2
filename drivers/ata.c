#include <stdint.h>

#include "ata.h"
#include "blkdev.h"
#include "io.h"
#include "serial.h"

#define ATA_TIMEOUT 100000u

static BlockDevice ata_device;
static uint32_t ata_total_sectors;
static int ata_present;

/* Render one small decimal value to serial for ATA probe logs. */
static void ata_write_u32(uint32_t value) {
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

/* Burn the ATA-required 400ns after each drive select. */
static void ata_delay_400ns(void) {
    io_in8(ATA_PRIMARY_STATUS);
    io_in8(ATA_PRIMARY_STATUS);
    io_in8(ATA_PRIMARY_STATUS);
    io_in8(ATA_PRIMARY_STATUS);
}

/* Poll until BSY clears and, when requested, DRQ becomes ready. */
static int ata_wait_ready(int require_drq) {
    uint32_t i;

    for (i = 0u; i < ATA_TIMEOUT; i++) {
        uint8_t status = io_in8(ATA_PRIMARY_STATUS);

        if ((status & ATA_STATUS_BSY) != 0u) {
            continue;
        }
        if ((status & ATA_STATUS_ERR) != 0u) {
            return -1;
        }
        if (!require_drq || (status & ATA_STATUS_DRQ) != 0u) {
            return 0;
        }
    }
    return -1;
}

/* Program one LBA28 sector transfer for the primary master disk. */
static int ata_select_lba28(uint32_t lba, uint8_t count) {
    if (!ata_present || count == 0u || lba >= ata_total_sectors || count > ata_total_sectors - lba) {
        return -1;
    }

    io_out8(ATA_PRIMARY_DRIVE, (uint8_t)(0xE0u | ((lba >> 24) & 0x0Fu)));
    ata_delay_400ns();
    io_out8(ATA_PRIMARY_ERROR, 0u);
    io_out8(ATA_PRIMARY_SECCOUNT, count);
    io_out8(ATA_PRIMARY_LBA_LO, (uint8_t)(lba & 0xFFu));
    io_out8(ATA_PRIMARY_LBA_MID, (uint8_t)((lba >> 8) & 0xFFu));
    io_out8(ATA_PRIMARY_LBA_HI, (uint8_t)((lba >> 16) & 0xFFu));
    return 0;
}

/* Read one or more 512-byte sectors from the ATA disk into a caller buffer. */
static int ata_block_read(BlockDevice *dev, uint32_t lba, uint32_t count, void *buf) {
    uint16_t *out = (uint16_t *)buf;
    uint32_t sector;
    uint32_t word;

    (void)dev;
    if (buf == (void *)0 || count == 0u || !ata_present || lba >= ata_total_sectors || count > ata_total_sectors - lba) {
        return -1;
    }

    for (sector = 0u; sector < count; sector++) {
        if (ata_select_lba28(lba + sector, 1u) != 0) {
            return -1;
        }
        io_out8(ATA_PRIMARY_CMD, ATA_CMD_READ_PIO);
        if (ata_wait_ready(1) != 0) {
            return -1;
        }
        for (word = 0u; word < 256u; word++) {
            out[(sector * 256u) + word] = io_in16(ATA_PRIMARY_DATA);
        }
    }
    return 0;
}

/* Write one or more 512-byte sectors from a caller buffer to the ATA disk. */
static int ata_block_write(BlockDevice *dev, uint32_t lba, uint32_t count, const void *buf) {
    const uint16_t *in = (const uint16_t *)buf;
    uint32_t sector;
    uint32_t word;

    (void)dev;
    if (buf == (const void *)0 || count == 0u || !ata_present || lba >= ata_total_sectors
        || count > ata_total_sectors - lba) {
        return -1;
    }

    for (sector = 0u; sector < count; sector++) {
        if (ata_select_lba28(lba + sector, 1u) != 0) {
            return -1;
        }
        io_out8(ATA_PRIMARY_CMD, ATA_CMD_WRITE_PIO);
        if (ata_wait_ready(1) != 0) {
            return -1;
        }
        for (word = 0u; word < 256u; word++) {
            io_out16(ATA_PRIMARY_DATA, in[(sector * 256u) + word]);
        }
        io_out8(ATA_PRIMARY_CMD, ATA_CMD_CACHE_FLUSH);
        if (ata_wait_ready(0) != 0) {
            return -1;
        }
    }
    return 0;
}

/* Probe the primary ATA master and register it as hd0 when IDENTIFY succeeds. */
int ata_init(void) {
    uint16_t identify[256];
    uint32_t i;
    int reg_index;

    ata_present = 0;
    ata_total_sectors = 0u;

    io_out8(ATA_PRIMARY_DRIVE, 0xA0u);
    ata_delay_400ns();
    io_out8(ATA_PRIMARY_SECCOUNT, 0u);
    io_out8(ATA_PRIMARY_LBA_LO, 0u);
    io_out8(ATA_PRIMARY_LBA_MID, 0u);
    io_out8(ATA_PRIMARY_LBA_HI, 0u);
    io_out8(ATA_PRIMARY_CMD, ATA_CMD_IDENTIFY);

    if (io_in8(ATA_PRIMARY_STATUS) == 0u) {
        serial_print("[ata] no primary master drive\n");
        return -1;
    }

    for (i = 0u; i < ATA_TIMEOUT; i++) {
        uint8_t status = io_in8(ATA_PRIMARY_STATUS);

        if ((status & ATA_STATUS_BSY) != 0u) {
            continue;
        }
        if (io_in8(ATA_PRIMARY_LBA_MID) != 0u || io_in8(ATA_PRIMARY_LBA_HI) != 0u) {
            serial_print("[ata] primary master is not ATA\n");
            return -1;
        }
        if ((status & ATA_STATUS_ERR) != 0u) {
            serial_print("[ata] IDENTIFY failed\n");
            return -1;
        }
        if ((status & ATA_STATUS_DRQ) != 0u) {
            break;
        }
    }
    if (i == ATA_TIMEOUT) {
        serial_print("[ata] IDENTIFY timeout\n");
        return -1;
    }

    for (i = 0u; i < 256u; i++) {
        identify[i] = io_in16(ATA_PRIMARY_DATA);
    }

    ata_total_sectors = ((uint32_t)identify[61] << 16) | (uint32_t)identify[60];
    if (ata_total_sectors == 0u || ata_total_sectors == 0xFFFFFFFFu) {
        serial_print("[ata] invalid sector count\n");
        return -1;
    }

    ata_device.name[0] = 'h';
    ata_device.name[1] = 'd';
    ata_device.name[2] = '0';
    ata_device.name[3] = '\0';
    ata_device.block_count = ata_total_sectors;
    ata_device.block_size = BLOCK_SIZE;
    ata_device.read = ata_block_read;
    ata_device.write = ata_block_write;
    ata_device.private_data = (void *)0;
    ata_device.present = 1;

    reg_index = blkdev_register(&ata_device);
    if (reg_index < 0) {
        serial_print("[ata] failed to register hd0\n");
        return -1;
    }

    ata_present = 1;
    serial_print("[ata] detected hd0 sectors=");
    ata_write_u32(ata_total_sectors);
    serial_print("\n");
    return 0;
}
