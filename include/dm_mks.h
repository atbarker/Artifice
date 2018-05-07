/**
 * Target file header for the matryoshka file system.
 * 
 * Author: Yash Gupta <ygupta@ucsc.edu>, Austen Barker <atbarker@ucsc.edu>
 * Copyright: UC Santa Cruz, SSRC
 */
#include <dm_mks_lib.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device-mapper.h>
#include <linux/stddef.h>
#include <linux/bio.h>
#include <linux/errno.h>
#include <linux/mm_types.h>
#include <linux/rslib.h>

#ifndef _DM_MKS_H
#define _DM_MKS_H

//
// Macros
//
// Metadata
#define DM_MKS_NAME             "mks"
#define DM_MKS_MAJOR_VER        0
#define DM_MKS_MINOR_VER        1
#define DM_MKS_PATCH_VER        0

// Sizes
#define DM_MKS_PASSPHRASE_SZ    128
#define DM_MKS_PASSIVE_DEV_SZ   128

//
// Enumerations
//
// Target arguments
enum mks_args {
    DM_MKS_ARG_PASSPHRASE = 0,
    DM_MKS_ARG_PASSIVE_DEV,
    DM_MKS_ARG_NEW,
    DM_MKS_ARG_MAX
};

// Supported file systems
enum mks_fs {
    DM_MKS_FS_FAT32 = 0,
    DM_MKS_FS_EXT = 1,
    DM_MKS_FS_NTFS = 2,
    DM_MKS_FS_MAX,
    DM_MKS_FS_NONE
};

//
// Structures
//
// Private data per instance
struct mks_private {
    // Target Arguments.
    char passphrase[DM_MKS_PASSPHRASE_SZ];
    char passive_dev_name[DM_MKS_PASSIVE_DEV_SZ];

    // Device mapper representation for the
    // passive device.
    struct dm_dev *passive_dev;

    // File system context.
    struct mks_fs_context *fs_context;
    struct mks_super *super;
};

//
// Prototypes
//
// Device mapper required
static int mks_ctr(struct dm_target *ti, unsigned int argc, char **argv);
static void mks_dtr(struct dm_target *ti);
static int mks_map(struct dm_target *ti, struct bio *bio);

// Matryoshka helpers
static int mks_detect_fs(struct block_device *device, struct mks_private *context);

#endif
