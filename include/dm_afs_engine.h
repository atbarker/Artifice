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
    // The array of pointers to blocks is populated by calling __get_free_page()
    // as Artifice blocks are the same size as Linux memory pages we can easily 
    // have aligned pages. On destruction these arrays should be cleaned by 
    // calling free_page() on each member.
    uint8_t *carrier_blocks[NUM_MAX_CARRIER_BLKS];
    uint8_t __attribute__((aligned(4096))) data_block[AFS_BLOCK_SIZE];

    // Multiple requests to the same block will need to be synchronized.
    atomic64_t state;

    // Parent data block bio and number of pending carrier block bios.
    struct bio *bio;
    atomic_t bios_pending;

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
    uint8_t sharenrs[NUM_MAX_CARRIER_BLKS];

    //data block number in the map and carrier block offsets
    uint32_t block;
    uint32_t request_size;
    uint32_t sector_offset;
    uint32_t block_nums[NUM_MAX_CARRIER_BLKS];
  
    //flag to mark if rebuild is required and array to keep track of block status
    //0 is the block is yet to be processed, 1 is the block is fine, 2 is corrupted
    //TODO set this flag
    atomic_t rebuild_flag;

    struct work_struct req_ws;
    //So that we can access the encapsulating engine queue struct
    struct afs_engine_queue *eq;

    //Intrusive red black tree node
    struct rb_node node;
};

// A map queue and its lock.
struct afs_engine_queue {
    struct afs_map_request mq;
    struct rb_root mq_tree;
    spinlock_t mq_lock;
};

/**
 * Cleanup a completed request.
 */
void afs_req_clean(struct afs_map_request *req);

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
void afs_eq_add(struct afs_engine_queue *eq, struct afs_map_request *element);

/**
 * Check if an engine queue is empty without locking.
 */
bool afs_eq_empty_unsafe(struct afs_engine_queue *eq);

/**
 * Check if an engine queue.
 */
bool afs_eq_empty(struct afs_engine_queue *eq);

/**
 * Remove a specified request from the red/black tree.
 */
void inline afs_eq_remove(struct afs_engine_queue *eq, struct afs_map_request *req);

/**
 * Check if an engine queue contains a request with a specified
 * bio.
 */
struct afs_map_request * afs_eq_req_exist(struct afs_engine_queue *eq, struct bio *bio);

#endif /* DM_AFS_ENGINE_H */
