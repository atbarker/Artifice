/*
 * Author: Fill in.
 * Copyright: Fill in.
 */
#include <dm_afs.h>
#include <dm_afs_modules.h>

/**
 * Detect the presence of an EXT4 file system
 * on 'device'.
 * 
 * @data    The first 4KB of the device.
 * @fs      The file system information to be filled in.
 * @return  boolean.
 */
bool
afs_ext4_detect(const void* data, struct block_device* device, struct afs_passive_fs* fs)
{
    return false;
}
