/**
 * Author: Yash Gupta <ygupta@ucsc.edu>, Austen Barker <atbarker@ucsc.edu>
 * Copyright: UC Santa Cruz, SSRC
 */
#include <dm_afs_config.h>
#include <dm_afs_format.h>
#include "lib/cauchy_rs.h"

#ifndef DM_AFS_IO_H
#define DM_AFS_IO_H

// I/O flags.
enum afs_io_type {
    IO_READ = 0,
    IO_WRITE
};

// Artifice Block Device I/O.
struct afs_io {
    struct block_device *bdev; // Block Device to issue I/O on.
    struct page *io_page;      // Kernel Page(s) used for transfer.
    sector_t io_sector;        // Disk sector used for transfer.
    uint32_t io_size;          // Size of I/O transfer.
    enum afs_io_type type;     // Read or write.
};

/**
 * Read or write to a block device.
 */
int afs_blkdev_io(struct afs_io *request);

/**
 * Read a single page.
 */
int read_page(void *page, struct block_device *bdev, uint32_t block_num, bool used_vmalloc);

/**
 * Write a single page.
 */
int write_page(const void *page, struct block_device *bdev, uint32_t block_num, bool used_vmalloc);

int afs_encode(struct afs_config *config, uint8_t** carrier_blocks, uint8_t** entropy_blocks, uint8_t** data_blocks);

int afs_decode(struct afs_config *config, uint8_t** carrier_blocks, uint8_t** entropy_blocks, uint8_t** data_blocks);

#endif /* DM_AFS_IO_H */
