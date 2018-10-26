/*
 * Author: Fill in.
 * Copyright: Fill in.
 */
#include <dm_afs_modules.h>
#include <dm_afs.h>

/**
 * Detect the presence of an NTFS file system
 * on 'device'.
 * 
 * @data    The first 4KB of the device.
 * @fs      The file system information to be filled in.
 * @return  boolean.
 */
bool 
afs_ntfs_detect(const void *data, struct block_device *device, struct afs_passive_fs *fs)
{
    return false;
}
