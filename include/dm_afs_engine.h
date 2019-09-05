/**
 * Author: Yash Gupta <ygupta@ucsc.edu>, Austen Barker <atbarker@ucsc.edu>
 * Copyright: UC Santa Cruz, SSRC
 */
#include <dm_afs_config.h>
#include <dm_afs_modules.h>
#include <lib/libgfshare.h>
#include <linux/types.h>

#ifndef DM_AFS_ENGINE_H
#define DM_AFS_ENGINE_H

enum {
    REQ_STATE_GROUND = 1 << 0,
    REQ_STATE_FLIGHT = 1 << 1,
    REQ_STATE_COMPLETED = 1 << 2,
};

struct afs_engine_queue;

// A mapping request used to handle a single bio.
struct afs_map_request {
    // At max, we can have eight carrier blocks. We do waste space
    // allocating all of them in case we didn't have to, but this
    // way it is easier to manage. These blocks needs to be page
    // aligned.
    // TODO make these double pointers aligned to page boundaries
    //uint8_t __attribute__((aligned(4096))) entropy_blocks[NUM_MAX_CARRIER_BLKS][AFS_BLOCK_SIZE];
    //uint8_t __attribute__((aligned(4096))) read_blocks[NUM_MAX_CARRIER_BLKS][AFS_BLOCK_SIZE];
    //uint8_t __attribute__((aligned(4096))) write_blocks[NUM_MAX_CARRIER_BLKS][AFS_BLOCK_SIZE];
    //uint8_t __attribute__((aligned(4096))) data_block[AFS_BLOCK_SIZE];
    uint8_t *carrier_blocks[NUM_MAX_CARRIER_BLKS];
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

    struct afs_map_tuple *map_entry_tuple;
    uint8_t *map_entry;
    uint8_t *map_entry_hash;
    uint8_t *map_entry_entropy;

    // Write requests allocate a new page for a bio.
    uint8_t *allocated_write_page;

    //encoding context and parameters
    gfshare_ctx *encoder;
    uint8_t *sharenrs;

    //double pointers because static 2d arrays have a different layout in memory
    //TODO get rid of this 
    //uint8_t **carrier_blocks;

    //data block number in the map
    uint32_t block;
    uint32_t request_size;
    uint32_t sector_offset;

    //carrier block offsets
    uint32_t block_nums[NUM_MAX_CARRIER_BLKS];
  
    //used to tell if a request is currently pending and return status on invalid block write
    //TODO create seperate invalid/pending flags
    atomic_t pending;

    //flag to mark if rebuild is required and array to keep track of block status
    //0 is the block is yet to be processed, 1 is the block is fine, 2 is corrupted
    //TODO set this flag
    atomic_t rebuild_flag;
} __attribute__((aligned(4096)));

// Map request queue. This is an intrusive
// linked list.
struct afs_map_queue {
    struct afs_map_request req;
    struct work_struct req_ws;
    struct work_struct *clean_ws;

    // Save the engine queue this request exists on so that
    // the queue may be accessed through it.
    struct afs_engine_queue *eq;

    // Intrusive linked list connector.
    struct list_head list;
};

// A map queue with its lock.
struct afs_engine_queue {
    struct afs_map_queue mq;
    spinlock_t mq_lock;
};

/**
 * Rebuild a set of corrupted blocks
 */
int rebuild_blocks(struct afs_map_request *req);

/**
 * Map a read request from userspace.
 */
int afs_read_request(struct afs_map_request *req, struct bio *bio);

/**
 * Map a write request from userspace.
 */
int afs_write_request(struct afs_map_request *req, struct bio *bio);

/**
 * Initialize an engine queue.
 */
void afs_eq_init(struct afs_engine_queue *eq);

/**
 * Add an element to an engine queue.
 */
void afs_eq_add(struct afs_engine_queue *eq, struct afs_map_queue *element);

/**
 * Check if an engine queue is empty without locking.
 */
bool afs_eq_empty_unsafe(struct afs_engine_queue *eq);

/**
 * Check if an engine queue.
 */
bool afs_eq_empty(struct afs_engine_queue *eq);

/**
 * Check if an engine queue contains a request with a specified
 * bio.
 */
bool afs_eq_req_exist(struct afs_engine_queue *eq, struct bio *bio);

#endif /* DM_AFS_ENGINE_H */
