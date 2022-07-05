/*
 * Author: Austen Barker
 * Copyright: 2020
 */
#include <dm_afs.h>
#include <dm_afs_modules.h>
#include <linux/vmalloc.h>

/**
 * Detect the presence of an existing Artifice file system
 * on 'device'.
 * 
 * @data    The first 4KB of the device.
 * @fs      The file system information to be filled in.
 * @return  boolean.
 */
bool
afs_shadow_detect(const void *data, struct block_device *device, struct afs_passive_fs *fs)
{
    return false;
}

