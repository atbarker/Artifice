/**
 * Author: Yash Gupta <ygupta@ucsc.edu>, Austen Barker <atbarker@ucsc.edu>
 * Copyright: UC Santa Cruz, SSRC
 */
#include <dm_afs_config.h>

#ifndef DM_AFS_ENGINE_H
#define DM_AFS_ENGINE_H

// A mapping request used to handle a single bio.
struct __attribute__((aligned(4096))) afs_map_request {
    // At max, we can have eight carrier blocks. We do waste space
    // allocating all of them in case we didn't have to, but this
    // way it is easier to manage. These blocks needs to be page
    // aligned.
    uint8_t entropy_blocks[NUM_MAX_CARRIER_BLKS][AFS_BLOCK_SIZE];
    uint8_t read_blocks[NUM_MAX_CARRIER_BLKS][AFS_BLOCK_SIZE];
    uint8_t write_blocks[NUM_MAX_CARRIER_BLKS][AFS_BLOCK_SIZE];
    uint8_t data_block[AFS_BLOCK_SIZE];

    // Multiple requests to the same block will need to be synchronized.
    spinlock_t sync_lock;
    struct bio *bio;
    struct work_struct work_request;

    // We need these from the instance context to process a request.
    uint8_t *map;
    struct block_device *bdev;
    struct afs_config *config;
    struct afs_passive_fs *fs;

    // TODO: Clean this up.
    spinlock_t *vec_lock;
    bit_vector_t *vec;
};

/**
 * Map a read request from userspace.
 */
int afs_read_request(struct afs_map_request *req, struct bio *bio);

/**
 * Map a write request from userspace.
 */
int afs_write_request(struct afs_map_request *req, struct bio *bio);

#endif /* DM_AFS_ENGINE_H */