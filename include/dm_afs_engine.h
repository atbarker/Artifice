/**
 * Author: Yash Gupta <ygupta@ucsc.edu>, Austen Barker <atbarker@ucsc.edu>
 * Copyright: UC Santa Cruz, SSRC
 */
#include <dm_afs_config.h>
#include <dm_afs_modules.h>
#include <linux/types.h>

#ifndef DM_AFS_ENGINE_H
#define DM_AFS_ENGINE_H

enum {
    REQ_STATE_TRANSMIT = 1 << 0,
    REQ_STATE_PROCESSING = 1 << 1,
    REQ_STATE_COMPLETED = 1 << 2,
};

// A mapping request used to handle a single bio.
struct afs_map_request {
    // At max, we can have eight carrier blocks. We do waste space
    // allocating all of them in case we didn't have to, but this
    // way it is easier to manage. These blocks needs to be page
    // aligned.
    uint8_t __attribute__((aligned(4096))) entropy_blocks[NUM_MAX_CARRIER_BLKS][AFS_BLOCK_SIZE];
    uint8_t __attribute__((aligned(4096))) read_blocks[NUM_MAX_CARRIER_BLKS][AFS_BLOCK_SIZE];
    uint8_t __attribute__((aligned(4096))) write_blocks[NUM_MAX_CARRIER_BLKS][AFS_BLOCK_SIZE];
    uint8_t __attribute__((aligned(4096))) data_block[AFS_BLOCK_SIZE];

    // Multiple requests to the same block will need to be synchronized.
    atomic64_t state;
    struct bio *bio;

    // We need these from the instance context to process a request.
    uint8_t *map;
    struct block_device *bdev;
    struct afs_config *config;
    struct afs_passive_fs *fs;
    struct afs_allocation_vector *vector;

    // Write requests allocate a new page for a bio.
    uint8_t *allocated_write_page;
};

// Map request queue.
struct afs_map_queue {
    struct afs_map_request req;
    struct work_struct element_work_struct;

    // Overall queue.
    struct afs_map_queue *queue;
    spinlock_t *queue_lock;

    // Queue connector.
    struct list_head list;
};

/**
 * Map a read request from userspace.
 */
int
afs_read_request(struct afs_map_request *req, struct bio *bio);

/**
 * Map a write request from userspace.
 */
int afs_write_request(struct afs_map_request *req, struct bio *bio);

/**
 * Add a map element to a map queue.
 */
void afs_add_map_queue(struct afs_map_queue *q, spinlock_t *q_lock, struct afs_map_queue *element);

#endif /* DM_AFS_ENGINE_H */