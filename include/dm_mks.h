/**
 * Target file header for the matryoshka file system.
 * 
 * Author: Yash Gupta <ygupta@ucsc.edu>, 
 * Copyright: UC Santa Cruz, SSRC
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device-mapper.h>
#include <linux/stddef.h>
#include <linux/bio.h>
#include <linux/errno.h>

#ifndef _DM_MKS_H_
#define _DM_MKS_H_

//
// Macros
//
// Metadata
#define DM_MKS_NAME             "mks"
#define DM_MKS_MAJOR_VER        0
#define DM_MKS_MINOR_VER        0
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
    DM_MKS_ARG_MAX
};

//
// Structures
//
// Private data per instance
struct mks_private {
    char passphrase[DM_MKS_PASSPHRASE_SZ];
    char passive_dev_name[DM_MKS_PASSIVE_DEV_SZ];

    struct dm_dev *passive_dev;
};

//
// Prototypes
//
static int mks_ctr(struct dm_target *ti, unsigned int argc, char **argv);
static void mks_dtr(struct dm_target *ti);
static int mks_map(struct dm_target *ti, struct bio *bio);

#endif