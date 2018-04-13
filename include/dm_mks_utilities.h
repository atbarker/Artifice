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
#include <dm_mks_lib.h>

#ifndef _DM_MKS_UTILITIES_H
#define _DM_MKS_UTILITIES_H

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
// Enumerations
//
// Matryoshka I/O flags
enum mks_io_flags {
    MKS_IO_READ = 0,
    MKS_IO_WRITE
};

//
// Structures
//
// Matryoshka I/O
struct mks_io {
    struct block_device *bdev;  // Block Device to issue I/O on.
    struct page *io_page;       // Kernel Page(s) used for transfer.
    sector_t io_sector;         // Disk sector used for transfer.
    u32 io_size;                // Size of I/O transfer.
};

//magical super block
struct mks_super{
    unsigned char hash[32];
    u64 mks_size;
    u8 ecc_scheme;
    u8 secret_split_type;
    u32 mks_map_start;
}__attribute__((packed));

//entry into a matryoshka map tuple
struct mks_map_tuple{
    u32 block_num;
    u16 hash;
}__attribute__((packed));

//Matryoshka Map entry
struct mks_map_entry{
    struct mks_map_tuples *tuples;
    unsigned char datablock_checksum[16];
}__attribute__((packed));

//
// Prototypes
//
unsigned long bsr(unsigned long n);
int mks_blkdev_io(struct mks_io *io_request, enum mks_io_flags flag);
int passphrase_hash(unsigned char *passphrase, unsigned int pass_len, unsigned char *digest);
int write_new_map(u32 entries, struct mks_fs_context *context);
struct mks_super * generate_superblock(unsigned char *digest, u64 mks_size, u8 ecc_scheme, u8 secret_split_type, u32 mks_map_start);
int write_new_superblock(struct mks_super *super, int duplicates, unsigned char *digest, struct mks_fs_context *context);
struct mks_super* retrieve_superblock(int duplicates, unsigned char *digest, struct mks_fs_context *context, struct block_device *device);

#endif
