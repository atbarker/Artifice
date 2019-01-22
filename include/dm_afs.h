/**
 * Author: Yash Gupta <ygupta@ucsc.edu>, Austen Barker <atbarker@ucsc.edu>
 * Copyright: UC Santa Cruz, SSRC
 */
#include <dm_afs_config.h>
#include <dm_afs_format.h>
#include <dm_afs_modules.h>
#include <lib/bit_vector.h>
#include <linux/bio.h>
#include <linux/completion.h>
#include <linux/device-mapper.h>
#include <linux/kernel.h>
#include <linux/mm_types.h>
#include <linux/module.h>
#include <linux/spinlock_types.h>
#include <linux/stddef.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/wait.h>

#ifndef DM_AFS_H
#define DM_AFS_H

// Global variables
extern int afs_debug_mode;

// A parsed structure of arguments received from the
// user.
struct afs_args {
    char passphrase[PASSPHRASE_SZ];        // Passphrase for this instance.
    char shadow_passphrase[PASSPHRASE_SZ]; // Passphrase in case this instance is a shadow.
    char passive_dev[PASSIVE_DEV_SZ];      // Name of passive device.
    char entropy_dir[ENTROPY_DIR_SZ];      // Name of the entropy directory.
    uint8_t instance_type;                 // Type of instance.
};

// Private data per instance. Do NOT change order
// of variables.
struct __attribute__((aligned(4096))) afs_private {
    uint8_t raw_block_read[AFS_BLOCK_SIZE];
    uint8_t raw_block_write[AFS_BLOCK_SIZE];
    struct afs_super_block super_block;
    struct afs_passive_fs passive_fs;
    struct afs_args instance_args;
    struct dm_dev *passive_dev;
    struct block_device *bdev;
    uint64_t instance_size;
    struct workqueue_struct *map_queue;
    struct task_struct *current_process;

    // Configuration information.
    uint8_t num_carrier_blocks;
    uint8_t map_entry_sz;
    uint8_t unused_space_per_block;
    uint8_t num_map_entries_per_block;
    uint32_t num_blocks;
    uint32_t num_map_blocks;
    uint32_t num_ptr_blocks;

    // Free list allocation vector.
    spinlock_t allocation_lock;
    bit_vector_t *allocation_vec;

    // Map information.
    uint8_t *afs_map;
    uint8_t *afs_map_blocks;
    struct afs_ptr_block *afs_ptr_blocks;
};

// A mapping request used to handle a single bio.
struct __attribute__((aligned(4096))) afs_map_request {
    uint8_t entropy_blocks[NUM_MAX_CARRIER_BLKS][AFS_BLOCK_SIZE];
    uint8_t read_blocks[NUM_MAX_CARRIER_BLKS][AFS_BLOCK_SIZE];
    uint8_t write_blocks[NUM_MAX_CARRIER_BLKS][AFS_BLOCK_SIZE];
    uint8_t data_block[AFS_BLOCK_SIZE];
    spinlock_t sync_lock;
    struct afs_private *context;
    struct bio *bio;
    struct work_struct work_request;
};

/**
 * Acquire a free block from the free list.
 */
uint32_t acquire_block(struct afs_passive_fs *fs, struct afs_private *context);

/**
 * Set the usage of a block in the allocation vector.
 */
bool allocation_set(struct afs_private *context, uint32_t index);

/**
 * Clear the usage of a block in the allocation vector.
 */
void allocation_free(struct afs_private *context, uint32_t index);

/**
 * Get the state of a block in the allocation vector.
 */
uint8_t allocation_get(struct afs_private *context, uint32_t index);

/**
 * Build the configuration for an instance.
 */
void build_configuration(struct afs_private *context, uint8_t num_carrier_blocks);

/**
 * Create the Artifice map and initialize it to
 * invalids.
 */
int afs_create_map(struct afs_private *context);

/**
 * Fill an Artifice map with values from the
 * metadata.
 */
int afs_fill_map(struct afs_super_block *sb, struct afs_private *context);

/**
 * Create the Artifice map blocks.
 */
int afs_create_map_blocks(struct afs_private *context);

/**
 * Write map blocks to pointer blocks.
 */
int write_map_blocks(struct afs_private *context, bool update);

/**
 * Write out the pointer blocks to disk.
 */
int write_ptr_blocks(struct afs_super_block *sb, struct afs_passive_fs *fs, struct afs_private *context);

/**
 * Write the super block onto the disk.
 */
int write_super_block(struct afs_super_block *sb, struct afs_passive_fs *fs, struct afs_private *context);

/**
 * Find the super block on the disk.
 */
int find_super_block(struct afs_super_block *sb, struct afs_private *context);

/**
 * Bit scan reverse.
 */
uint64_t bsr(uint64_t n);

/**
 * Acquire a SHA1 hash of given data.
 */
int hash_sha1(const void *data, const uint32_t data_len, uint8_t *digest);

/**
 * Acquire a SHA256 hash of given data.
 */
int hash_sha256(const void *data, const uint32_t data_len, uint8_t *digest);

/**
 * Acquire a SHA512 hash of given data.
 */
int hash_sha512(const void *data, const uint32_t data_len, uint8_t *digest);

/**
 * Map a read request from userspace.
 */
int afs_read_request(struct afs_map_request *req, struct bio *bio);

/**
 * Map a write request from userspace.
 */
int afs_write_request(struct afs_map_request *req, struct bio *bio);

#endif /* DM_AFS_H */