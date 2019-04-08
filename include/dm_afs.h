/**
 * Author: Yash Gupta <ygupta@ucsc.edu>, Austen Barker <atbarker@ucsc.edu>
 * Copyright: UC Santa Cruz, SSRC
 */
#include <dm_afs_config.h>
#include <dm_afs_engine.h>
#include <dm_afs_format.h>
#include <dm_afs_modules.h>
#include "lib/cauchy_rs.h"
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

// Private data per instance.
struct afs_private {
    struct dm_dev *passive_dev;
    struct block_device *bdev;
    struct afs_config config;
    struct afs_super_block __attribute__((aligned(4096))) super_block;
    struct afs_passive_fs passive_fs;
    struct afs_args args;
    struct afs_allocation_vector vector;

    // I/O requests are handled by a two level queueing system. Work
    // structs for the flight queue are within the request structure
    // itself.
    //
    // All requests provided to the map callback are added to the
    // ground queue. A thread dequeues elements from the ground
    // queue and adds them to the flight queue. Only requests which
    // are part of the flight queue are being actively processed. In
    // case a request for a certain block is already part of the
    // flight queue, it is skipped over for selection in the ground
    // queue until the request in the flight queue has been completed.
    //
    // After a request has been processed, a thread is called which
    // cleans up the flight queue. This thread does not require its
    // separate workqueue since the kernel workqueue is used for it.
    struct afs_engine_queue ground_eq;
    struct afs_engine_queue flight_eq;
    struct workqueue_struct *ground_wq;
    struct workqueue_struct *flight_wq;
    struct work_struct ground_ws;
    struct work_struct clean_ws;

    // Map information.
    uint8_t *afs_map;
    uint8_t *afs_map_blocks;
    struct afs_ptr_block *afs_ptr_blocks;

    //Encoding stuff
    cauchy_encoder_params params;
};

/**
 * Build the configuration for an instance.
 */
void build_configuration(struct afs_private *context, uint8_t num_carrier_blocks, uint8_t num_entropy_blocks);

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
 * Perform a reverse bit scan for an unsigned long.
 * Primarily used to verify data sizes for file system processing
 */
static inline uint64_t
bsr(uint64_t n)
{
    __asm__("bsr %1,%0"
            : "=r"(n)
            : "rm"(n));

    return n;
}

#endif /* DM_AFS_H */
