/**
 * Target file header for the matryoshka file system.
 * 
 * Author: 
 * Copyright: UC Santa Cruz, SSRC
 * 
 * License: 
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device-mapper.h>

//
// Macros
//
// Metadata
#define DM_MKS_NAME         "mks"
#define DM_MKS_MAJOR_VER    0
#define DM_MKS_MINOR_VER    0
#define DM_MKS_PATCH_VER    0

// Printing and debugging.
#define dm_mks_info(fmt, ...)                                   \
    do {                                                        \
        printk(KERN_INFO "dm-mks-info: " fmt, ##__VA_ARGS__);   \
    } while (0)

#define dm_mks_debug(fmt, ...)                                  \
    do {                                                        \
        if (debug_enable) {                                     \
            printk(KERN_DEBUG "dm-mks-debug: [%s:%d] " fmt,     \
            __func__, __LINE__,                                 \
            ##__VA_ARGS__);                                     \
        }                                                       \
    } while (0)

#define dm_mks_alert(fmt, ...)                              \
    do {                                                    \
        printk(KERN_ALERT "dm-mks-alert: [%s:%d] " fmt,     \
        __func__, __LINE__,                                 \
        ##__VA_ARGS__);                                     \
    } while (0)

//
// Enumerations
//
// Target arguments
enum dm_mks_args {
    DM_MKS_ARG_PASSPHRASE = 0,
    DM_MKS_ARG_BLOCKDEV,
    DM_MKS_ARG_END
};

//
// Global variables
//
// Debug enable
static int debug_enable = 0;

//
// Prototypes
//
static int dm_mks_ctr(struct dm_target *ti, unsigned int argc, char **argv);
static void dm_mks_dtr(struct dm_target *ti);
static int dm_mks_map(struct dm_target *ti, struct bio *bio);