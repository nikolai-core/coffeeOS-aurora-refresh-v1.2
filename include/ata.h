#ifndef ATA_H
#define ATA_H

#include <stdint.h>

#define ATA_PRIMARY_DATA     0x1F0u
#define ATA_PRIMARY_ERROR    0x1F1u
#define ATA_PRIMARY_SECCOUNT 0x1F2u
#define ATA_PRIMARY_LBA_LO   0x1F3u
#define ATA_PRIMARY_LBA_MID  0x1F4u
#define ATA_PRIMARY_LBA_HI   0x1F5u
#define ATA_PRIMARY_DRIVE    0x1F6u
#define ATA_PRIMARY_STATUS   0x1F7u
#define ATA_PRIMARY_CMD      0x1F7u

#define ATA_CMD_READ_PIO   0x20u
#define ATA_CMD_WRITE_PIO  0x30u
#define ATA_CMD_CACHE_FLUSH 0xE7u
#define ATA_CMD_IDENTIFY   0xECu

#define ATA_STATUS_BSY 0x80u
#define ATA_STATUS_DRQ 0x08u
#define ATA_STATUS_ERR 0x01u

/* Detect a primary ATA disk and register it as a block device when present. */
int ata_init(void);

#endif
