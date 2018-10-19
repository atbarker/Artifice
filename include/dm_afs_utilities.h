/**
 * Author: Yash Gupta <ygupta@ucsc.edu>, Austen Barker <atbarker@ucsc.edu>
 * Copyright: UC Santa Cruz, SSRC
 */
#include <linux/stddef.h>
#include <linux/bio.h>
#include <linux/completion.h>
#include <linux/mm_types.h>
#include <linux/version.h>
#include <linux/types.h>

#ifndef DM_AFS_UTILITIES_H
#define DM_AFS_UTILITIES_H

// Standard integer types.
typedef s8 int8_t;
typedef u8 uint8_t;
typedef s16 int16_t;
typedef u16 uint16_t;
typedef s32 int32_t;
typedef u32 uint32_t;
typedef s64 int64_t;
typedef u64 uint64_t;

// Global variables
extern int afs_debug_mode;

// Simple information.
#define afs_info(fmt, ...)                                  \
({                                                          \
    printk(KERN_INFO "dm-afs-info: " fmt, ##__VA_ARGS__);   \
})

// Debug information.
#define afs_debug(fmt, ...)                                 \
({                                                          \
    if (afs_debug_mode) {                                   \
        printk(KERN_DEBUG "dm-afs-debug: [%s:%d] " fmt,     \
        __func__, __LINE__,                                 \
        ##__VA_ARGS__);                                     \
    }                                                       \
})

// Alert information.
#define afs_alert(fmt, ...)                                 \
({                                                          \
    printk(KERN_ALERT "dm-afs-alert: [%s:%d] " fmt,         \
    __func__, __LINE__,                                     \
    ##__VA_ARGS__);                                         \
})

// Assert and jump.
#define afs_assert(cond, label, fmt, args ...)  \
({                                              \
    if (!(cond)) {                              \
        afs_alert(fmt, ##args);                 \
        goto label;                             \
    }                                           \
})

// Assert, perform an action, and jump.
#define afs_assert_action(cond, action, label, fmt, args ...)  \
({                                                              \
    if (!(cond)) {                                              \
        action;                                                 \
        afs_alert(fmt, ##args);                                 \
        goto label;                                             \
    }                                                           \
})

// Disk I/O flags
enum afs_io_type {
    IO_READ = 0,
    IO_WRITE
};

// Artifice I/O
struct afs_io {
    struct block_device *bdev;      // Block Device to issue I/O on.
    struct page         *io_page;   // Kernel Page(s) used for transfer.
    sector_t            io_sector;  // Disk sector used for transfer.
    uint32_t            io_size;    // Size of I/O transfer.
    enum afs_io_type    type;       // Read or write.
};

// Passive file system information.
struct afs_passive_fs {
    uint32_t    *block_list;        // List of empty blocks.
    uint32_t    list_len;           // Length of that list.
	uint8_t	    sectors_per_block;  // Sectors in a block.
	uint32_t	total_blocks;       // Total number of blocks in the FS.
    uint32_t    data_start_off;     // Data start offset in the filesystem (bypass reserved blocks).
	uint8_t     blocks_in_tuple;    // Blocks in a tuple.
    uint8_t     *allocation;        // Allocation bitmap.
};

/**
 * Read or write to an block device.
 */
int afs_blkdev_io(struct afs_io *request);

// //magical super block
// //need to add an array to store the superblock copies, most likely hard coded.
// struct afs_super{
//     unsigned char hash[32];
//     u64 afs_size;
//     u8 ecc_scheme;
//     u8 secret_split_type;
//     u32 afs_map_start;
// }__attribute__((packed));

// //entry into a matryoshka map tuple
// struct afs_map_tuple{
//     u32 block_num;
// //    u16 checksum;
// }__attribute__((packed));

// //Artifice Map entry
// struct afs_map_entry{
//     u32 block_num;
//     struct afs_map_tuple tuples[8];
//     unsigned char datablock_checksum[16];
// }__attribute__((packed));

unsigned long bsr(unsigned long n);
// int random_offset(u32 upper_limit);
// int passphrase_hash(unsigned char *passphrase, unsigned int pass_len, unsigned char *digest);
// struct afs_map_entry* write_new_map(u32 entries, struct afs_fs_context *context, struct block_device *device, u32 first_offset);
// struct afs_map_entry* retrieve_map(u32 entries, struct afs_fs_context *context, struct block_device *device, struct afs_super *super);
// struct afs_super * generate_superblock(unsigned char *digest, u64 afs_size, u8 ecc_scheme, u8 secret_split_type, u32 afs_map_start);
// int write_new_superblock(struct afs_super *super, int duplicates, unsigned char *digest, struct afs_fs_context *context, struct block_device *device);
// struct afs_super* retrieve_superblock(int duplicates, unsigned char *digest, struct afs_fs_context *context, struct block_device *device);

#endif /* DM_AFS_UTILITIES_H */
