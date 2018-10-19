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

#ifndef DM_AFS_COMMON_H
#define DM_AFS_COMMON_H

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

/**
 * Collection of constants ranging from array sizes
 * to instance types.
 */
enum {
    // Configuration.
    AFS_MIN_SIZE    = 1 << 16,
    AFS_BLOCK_SIZE  = 4096,
    AFS_SECTOR_SIZE = 512,

    // Array sizes.
    PASSPHRASE_SZ   = 64,
    PASSIVE_DEV_SZ  = 32,
    ENTROPY_DIR_SZ  = 64,
    ENTROPY_HASH_SZ = 8,
    SB_MAP_PTRS_SZ  = 983,
    MAP_BLK_PTRS_SZ = 1023,

    // Hash algorithms
    SHA1_SZ   = 20,
    SHA128_SZ = 16,
    SHA256_SZ = 32,

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

// Artifice super block.
struct __attribute__((packed)) afs_super_block {
    uint8_t     hash[SHA1_SZ];  // Hash of the passphrase.
    uint64_t    instance_size;  // Size of this Artifice instance.
    uint8_t     reserved[4];    // TODO: Replace with RS information.
    char        entropy_dir[ENTROPY_DIR_SZ];        // Entropy directory for this instance.
    char        shadow_passphrase[PASSPHRASE_SZ];   // In case this instance is a nested instance.
    uint32_t    map_table_pointers[SB_MAP_PTRS_SZ]; // The super block stores the pointers to the first 983 map tables.
    uint32_t    next_map_block; // Pointer to the next map block in the chain.
};

// Artifice map block.
struct __attribute__((packed)) afs_map_block {
    // Each map block is 4KB. With 32 bit pointers,
    // we can store 1023 pointers to map tables and
    // a pointer to the next block.

    uint32_t map_table_pointers[MAP_BLK_PTRS_SZ];
    uint32_t next_map_block;
};

// Artifice map tuple.
struct __attribute__((packed)) afs_map_tuple {
    uint32_t    carrier_block_pointer;  // Sector number from the free list.
    uint32_t    entropy_block_pointer;  // Sector number from the entropy file.
    uint16_t    checksum;               // Checksum of this carrier block.
};

// Artifice map entry.
struct afs_map_entry {
    // The number of carrier blocks per data block
    // is configurable and hence this needs to be
    // a pointer.
    //
    // Each data block will draw entropy from a single
    // file. Each carrier block will have it's own
    // sector offset for this file.

    uint8_t num_tuples;
    struct afs_map_tuple *block_tuples;     // List of tuples for this data block.
    uint8_t block_hash[SHA128_SZ];          // Hash of the data block.
    uint8_t entropy_hash[ENTROPY_HASH_SZ];  // Hash of the entropy file name.
};

// Artifice map table (per block).
struct afs_map_table {
    // This structure represents all the map entires in
    // a single 4KB block. As the size of a map entry is
    // variable, we need to make space for some unused
    // portion of the block. Unused space is always kept
    // at the beginning of the block.

    uint8_t    unused_space;
    uint8_t    num_map_entries;
    uint8_t     *unused;
    struct afs_map_entry *map_entries;
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
 * Read or write to a block device.
 */
int afs_blkdev_io(struct afs_io *request);

/**
 * Read a single page.
 */
int read_page(uint8_t *page, struct block_device *bdev, uint32_t block_num);

/**
 * Write a single page.
 */
int write_page(const uint8_t *page, struct block_device *bdev, uint32_t block_num);

/**
 * Acquire a SHA1 hash of given data.
 */
int hash_sha1(const uint8_t *data, const uint32_t data_len, uint8_t *digest);

unsigned long bsr(unsigned long n);
// int random_offset(u32 upper_limit);
// int passphrase_hash(unsigned char *passphrase, unsigned int pass_len, unsigned char *digest);
// struct afs_map_entry* write_new_map(u32 entries, struct afs_fs_context *context, struct block_device *device, u32 first_offset);
// struct afs_map_entry* retrieve_map(u32 entries, struct afs_fs_context *context, struct block_device *device, struct afs_super *super);
// struct afs_super * generate_superblock(unsigned char *digest, u64 afs_size, u8 ecc_scheme, u8 secret_split_type, u32 afs_map_start);
// int write_new_superblock(struct afs_super *super, int duplicates, unsigned char *digest, struct afs_fs_context *context, struct block_device *device);
// struct afs_super* retrieve_superblock(int duplicates, unsigned char *digest, struct afs_fs_context *context, struct block_device *device);

#endif /* DM_AFS_COMMON_H */