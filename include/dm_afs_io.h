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
 * The sector offset argument is for just in case everything is not block aligned (FAT32)
 */
int read_page(void *page, struct block_device *bdev, uint32_t block_num, uint32_t sector_offset, bool used_vmalloc);

/**
 * Write a single page.
 * The sector offset argument is for just in case everything is not block alinged (FAT32)
 */
int write_page(const void *page, struct block_device *bdev, uint32_t block_num, uint32_t sector_offset,  bool used_vmalloc);

#endif /* DM_AFS_IO_H */
