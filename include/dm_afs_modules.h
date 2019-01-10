/*
 * Author: Yash Gupta, Austen Barker
 * Copyright: UC Santa Cruz, SSRC
 */
#include <dm_afs.h>
#include <linux/bio.h>
#include <linux/string.h>

#ifndef DM_AFS_MODULES_H
#define DM_AFS_MODULES_H

// File system detection functions.
bool afs_fat32_detect(const void *data, struct block_device *device, struct afs_passive_fs *fs);
bool afs_ext4_detect(const void *data, struct block_device *device, struct afs_passive_fs *fs);
bool afs_ntfs_detect(const void *data, struct block_device *device, struct afs_passive_fs *fs);

#endif /* DM_AFS_MODULES_H */