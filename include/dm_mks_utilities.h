/**
 * Basic utility system for miscellaneous functions.
 * 
 * Author: Yash Gupta <ygupta@ucsc.edu>
 * Copyright: UC Santa Cruz, SSRC
 */
#include <linux/stddef.h>
#include <linux/bio.h>
#include <linux/completion.h>
#include <linux/mm_types.h>
#include <linux/version.h>

#ifndef _DM_MKS_UTILITIES_H_
#define _DM_MKS_UTILITIES_H_

//
// Macros
//
// Printing and debugging.
#define DM_MKS_DEBUG_ENABLE     1
#define DM_MKS_DEBUG_DISABLE    0
#define __mks_get_debug()       mks_debug_mode
#define __mks_set_debug(mode)                                   \
    {                                                           \
        mks_debug_mode = mode;                                  \
    }

#define mks_info(fmt, ...)                                      \
    do {                                                        \
        printk(KERN_INFO "dm-mks-info: " fmt, ##__VA_ARGS__);   \
    } while (0)

#define mks_debug(fmt, ...)                                     \
    do {                                                        \
        if (mks_debug_mode) {                                   \
            printk(KERN_DEBUG "dm-mks-debug: [%s:%d] " fmt,     \
            __func__, __LINE__,                                 \
            ##__VA_ARGS__);                                     \
        }                                                       \
    } while (0)

#define mks_alert(fmt, ...)                                 \
    do {                                                    \
        printk(KERN_ALERT "dm-mks-alert: [%s:%d] " fmt,     \
        __func__, __LINE__,                                 \
        ##__VA_ARGS__);                                     \
    } while (0)

//
// Global variables
//
// Debug enable
static int mks_debug_mode = 1;

//
// Prototypes
//
inline unsigned long bsr(unsigned long n);
int mks_read_blkdev(struct block_device *bdev, struct page *dest, sector_t sector, u32 size);
int mks_write_blkdev(struct block_device *bdev, struct page *src, sector_t sector, u32 size);

#endif
