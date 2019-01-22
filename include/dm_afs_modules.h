/*
 * Author: Yash Gupta, Austen Barker
 * Copyright: UC Santa Cruz, SSRC
 */
#include <dm_afs_io.h>
#include <linux/bio.h>
#include <linux/string.h>

#ifndef DM_AFS_MODULES_H
#define DM_AFS_MODULES_H

// Passive file system information.
struct afs_passive_fs {
    uint32_t *block_list;      // List of empty blocks.
    uint32_t list_len;         // Length of that list.
    uint8_t sectors_per_block; // Sectors in a block.
    uint32_t total_blocks;     // Total number of blocks in the FS.
    uint32_t data_start_off;   // Data start offset in the filesystem (bypass reserved blocks).
    uint8_t blocks_in_tuple;   // Blocks in a tuple.
};

// File system detection functions.
bool afs_fat32_detect(const void *data, struct block_device *device, struct afs_passive_fs *fs);
bool afs_ext4_detect(const void *data, struct block_device *device, struct afs_passive_fs *fs);
bool afs_ntfs_detect(const void *data, struct block_device *device, struct afs_passive_fs *fs);

#endif /* DM_AFS_MODULES_H */