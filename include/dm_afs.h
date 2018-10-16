/**
 * Author: Yash Gupta <ygupta@ucsc.edu>, Austen Barker <atbarker@ucsc.edu>
 * Copyright: UC Santa Cruz, SSRC
 */
#include <lib/bit_vector.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/bio.h>
#include <linux/completion.h>
#include <linux/mm_types.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/device-mapper.h>
#include <linux/spinlock_types.h>

#ifndef DM_AFS_H
#define DM_AFS_H

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

// Metadata
#define DM_AFS_NAME             "artifice"
#define DM_AFS_MAJOR_VER        0
#define DM_AFS_MINOR_VER        1
#define DM_AFS_PATCH_VER        0

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
    AFS_MIN_SIZE      = 1 << 16,
    AFS_BLOCK_SIZE    = 4096,
    AFS_SECTOR_SIZE   = 512,
    AFS_INVALID_BLOCK = (((uint64_t)1 << 32) - 1),

    // Array sizes.
    PASSPHRASE_SZ   = 64,
    PASSIVE_DEV_SZ  = 32,
    ENTROPY_DIR_SZ  = 64,
    ENTROPY_HASH_SZ = 8,
    SB_MAP_PTRS_SZ  = 975,
    MAP_BLK_PTRS_SZ = 1019,

    // Hash algorithms
    SHA1_SZ   = 20,
    SHA128_SZ = 16,
    SHA256_SZ = 32,
    SHA512_SZ = 64,

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
    uint8_t     sb_hash[SHA256_SZ]; // Hash of the superblock.
    uint8_t     hash[SHA1_SZ];      // Hash of the passphrase.
    uint64_t    instance_size;      // Size of this Artifice instance.
    uint8_t     reserved[4];        // TODO: Replace with RS information.
    char        entropy_dir[ENTROPY_DIR_SZ];        // Entropy directory for this instance.
    char        shadow_passphrase[PASSPHRASE_SZ];   // In case this instance is a nested instance.
    uint32_t    map_table_pointers[SB_MAP_PTRS_SZ]; // The super block stores the pointers to the first 983 map tables.
    uint32_t    next_map_block; // Pointer to the next map block in the chain.
};

// Artifice map block.
struct __attribute__((packed)) afs_map_block {
    // Each map block is 4KB. With 32 bit pointers,
    // we can store 1019 pointers to map tables and
    // a pointer to the next block.

    uint8_t  hash[SHA128_SZ];
    uint32_t map_table_pointers[MAP_BLK_PTRS_SZ];
    uint32_t next_map_block;
};

// Artifice map tuple.
struct __attribute__((packed)) afs_map_tuple {
    uint32_t    carrier_block_pointer;  // Sector number from the free list.
    uint32_t    entropy_block_pointer;  // Sector number from the entropy file.
    uint16_t    checksum;               // Checksum of this carrier block.
};

// afs_map_entry
//
// We cannot create a struct out of this since
// the number of carrier blocks is user defined.
//
// Overall Size: 24 + (10 * num_carrier_blocks) bytes.
//
// Structure:
// afs_map_tuple[0] (10 bytes)
// afs_map_tuple[1] (10 bytes)
// .
// .
// .
// afs_map_tuple[num_carrier_blocks-1] (10 bytes)
// hash (16 bytes)
// Entropy file name hash (8 bytes)

// afs_map_table
//
// We cannot create a struct out of this since
// the size of an afs_map_entry is variable based
// on user configuration.
//
// Overall Size: Exactly 4096 bytes.
//
// Structure:
// hash (64 bytes)
// unused space (unused_space_per_table bytes)
// afs_map_entry[0] (map_entry_sz bytes)
// afs_map_entry[1] (map_entry_sz bytes)
// .
// .
// .
// afs_map_entry[num_map_entries_per_table-1] (map_entry_sz bytes)

// Artifice map table (per block).
struct afs_map_table {
    // This structure represents all the map entires in
    // a single 4KB block. As the size of a map entry is
    // variable, we need to make space for some unused
    // portion of the block. Unused space is always kept
    // at the beginning of the block.

    uint8_t hash[SHA512_SZ];
    uint8_t unused_space;
    uint8_t num_map_entries;
    uint8_t *unused;
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

// A parsed structure of arguments received from the
// user.
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
    struct block_device    *bdev;
    struct afs_passive_fs  passive_fs;
    struct afs_super_block super_block;

    // Configuration information.
    uint8_t  num_carrier_blocks;
    uint8_t  map_entry_sz;
    uint8_t  unused_space_per_table;
    uint8_t  num_map_entries_per_table;
    uint32_t num_blocks;
    uint32_t num_map_tables;
    uint32_t num_map_blocks;

    // Free list allocation vector.
    spinlock_t  allocation_lock;
    bit_vector_t *allocation_vec;

    // Map information.
    uint8_t *afs_map;
    uint8_t *afs_map_tables;
    struct afs_map_block *afs_map_blocks;
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

/**
 * Write the super block onto the disk.
 */
int write_super_block(struct afs_super_block *sb, struct afs_passive_fs *fs, struct afs_private *context);

/**
 * Acquire a free block from the free list.
 */
uint32_t acquire_block(struct afs_passive_fs *fs, struct afs_private *context);

/**
 * Bit scan reverse.
 */
unsigned long bsr(unsigned long n);

// int random_offset(u32 upper_limit);
// int passphrase_hash(unsigned char *passphrase, unsigned int pass_len, unsigned char *digest);
// struct afs_map_entry* write_new_map(u32 entries, struct afs_fs_context *context, struct block_device *device, u32 first_offset);
// struct afs_map_entry* retrieve_map(u32 entries, struct afs_fs_context *context, struct block_device *device, struct afs_super *super);
// struct afs_super * generate_superblock(unsigned char *digest, u64 afs_size, u8 ecc_scheme, u8 secret_split_type, u32 afs_map_start);
// int write_new_superblock(struct afs_super *super, int duplicates, unsigned char *digest, struct afs_fs_context *context, struct block_device *device);
// struct afs_super* retrieve_superblock(int duplicates, unsigned char *digest, struct afs_fs_context *context, struct block_device *device);

#endif /* DM_AFS_H */