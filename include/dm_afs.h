/**
 * Author: Yash Gupta <ygupta@ucsc.edu>, Austen Barker <atbarker@ucsc.edu>
 * Copyright: UC Santa Cruz, SSRC
 */
#include <dm_afs_utilities.h>
#include <dm_afs_modules.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device-mapper.h>
#include <linux/stddef.h>
#include <linux/bio.h>
#include <linux/errno.h>
#include <linux/mm_types.h>
#include <linux/rslib.h>

#ifndef DM_AFS_H
#define DM_AFS_H

// Metadata
#define DM_AFS_NAME             "artifice"
#define DM_AFS_MAJOR_VER        0
#define DM_AFS_MINOR_VER        1
#define DM_AFS_PATCH_VER        0

/**
 * Collection of constants ranging from array sizes
 * to instance types.
 */
enum {
    // Array sizes.
    PASSPHRASE_SZ   =   64,
    PASSIVE_DEV_SZ  =   32,
    ENTROPY_DIR_SZ  =   64,

    // Artifice type.
    TYPE_NEW    = 0,
    TYPE_ACCESS = 1,
    TYPE_SHADOW = 2,

    // File system support.
    FS_FAT32  = 0,
    FS_EXT4   = 1,
    FS_NTFS   = 2,
    FS_ERR    = -1
};

/**
 * A parsed structure of arguments received from the
 * user.
 */
struct afs_args {
    char    passphrase[PASSPHRASE_SZ];          // Passphrase for this instance.
    char    shadow_passphrase[PASSPHRASE_SZ];   // Passphrase in case this instance is a shadow.
    char    passive_dev[PASSIVE_DEV_SZ];        // Name of passive device.
    char    entropy_dir[ENTROPY_DIR_SZ];        // Name of the entropy directory.
    uint8_t instance_type;                      // Type of instance.
};

// Private data per instance.
struct afs_private {
    struct afs_args instance_args;
    struct dm_dev   *passive_dev;

    // Passive file system context.
    struct afs_passive_fs *passive_fs;
    // struct afs_super *super;
    // struct afs_map_entry *map;
};

#endif /* DM_AFS_H */
