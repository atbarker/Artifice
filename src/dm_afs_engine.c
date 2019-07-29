/**
 * Author: Yash Gupta <ygupta@ucsc.edu>, Austen Barker <atbarker@ucsc.edu>
 * Copyright: UC Santa Cruz, SSRC
 */
#include <dm_afs.h>
#include <dm_afs_config.h>
#include <dm_afs_engine.h>
#include <dm_afs_io.h>
#include <linux/delay.h>
#include <linux/timekeeping.h>
#include "lib/cauchy_rs.h"
#include "lib/libgfshare.h"
#include "lib/city.h"

/**
 * Convert 2 dimensional static array to double pointer 2d array.
 * TODO this can be thrown out once we get map request block arrays as double pointers instead of static arrays
 */
static inline void arraytopointer(uint8_t array[][AFS_BLOCK_SIZE], int size, uint8_t* output[AFS_BLOCK_SIZE]){
    int i;
    for(i = 0; i < size; i++){
        output[i] = array[i];
    }
}

/**
 * Initialize an engine queue.
 */
void
afs_eq_init(struct afs_engine_queue *eq)
{
    INIT_LIST_HEAD(&eq->mq.list);
    spin_lock_init(&eq->mq_lock);
}

/**
 * Add a map element to a map queue.
 */
void
afs_eq_add(struct afs_engine_queue *eq, struct afs_map_queue *element)
{
    spin_lock(&eq->mq_lock);
    list_add_tail(&element->list, &eq->mq.list);
    spin_unlock(&eq->mq_lock);
}

/**
 * Check if an engine queue is empty without locking.
 */
bool
afs_eq_empty_unsafe(struct afs_engine_queue *eq)
{
    return list_empty(&eq->mq.list);
}

/**
 * Check if an engine queue is empty.
 */
bool
afs_eq_empty(struct afs_engine_queue *eq)
{
    bool ret;

    spin_lock(&eq->mq_lock);
    ret = afs_eq_empty_unsafe(eq);
    spin_unlock(&eq->mq_lock);

    return ret;
}

/**
 * Check if an engine queue contains a request with a specified
 * bio.
 */
bool
afs_eq_req_exist(struct afs_engine_queue *eq, struct bio *bio)
{
    bool ret = false;
    struct afs_map_queue *node;
    struct bio *node_bio;
    uint32_t block_num;
    uint32_t node_block_num;

    block_num = (bio->bi_iter.bi_sector * AFS_SECTOR_SIZE) / AFS_BLOCK_SIZE;
    spin_lock(&eq->mq_lock);
    list_for_each_entry (node, &eq->mq.list, list) {
        node_bio = node->req.bio;
        node_block_num = (node_bio->bi_iter.bi_sector * AFS_SECTOR_SIZE) / AFS_BLOCK_SIZE;
        if (block_num == node_block_num && atomic64_read(&node->req.state) == REQ_STATE_FLIGHT) {
            ret = true;
            break;
        }
    }
    spin_unlock(&eq->mq_lock);

    return ret;
}

/**
 * Get a pointer to a map entry in the Artifice map.
 * TODO: Protect by a lock.
 */
static inline uint8_t *
afs_get_map_entry(uint8_t *map, struct afs_config *config, uint32_t index)
{
    return map + (index * config->map_entry_sz);
}

/**
 * Read a block from the map.
 * In case a block is unmapped, zero-fill it.
 */
static int
__afs_read_block(struct afs_map_request *req, uint32_t block)
{
    struct afs_config *config = NULL;
    struct afs_map_tuple *map_entry_tuple = NULL;
    uint8_t *map_entry = NULL;
    uint8_t *map_entry_hash = NULL;
    uint8_t *map_entry_entropy = NULL;
    uint8_t digest[SHA1_SZ];
    //uint8_t *digest;
    int ret, i;
    //TODO needs to calculate sharenrs and adjust as needed
    uint8_t* sharenrs = "0123";
    gfshare_ctx *share_decode = NULL;

    uint8_t** carrier_blocks;
    uint32_t *block_nums;

    config = req->config;
    //TODO change this when entropy handling is added
    carrier_blocks = kmalloc(sizeof(uint8_t*)*config->num_carrier_blocks, GFP_KERNEL);
    block_nums = kmalloc(sizeof(uint32_t) * config->num_carrier_blocks, GFP_KERNEL);
    share_decode = gfshare_ctx_init_dec(sharenrs, config->num_carrier_blocks, 2, AFS_BLOCK_SIZE);

    //set up map entry stuff
    map_entry = afs_get_map_entry(req->map, config, block);
    map_entry_tuple = (struct afs_map_tuple *)map_entry;
    map_entry_hash = map_entry + (config->num_carrier_blocks * sizeof(*map_entry_tuple));
    map_entry_entropy = map_entry_hash + SHA128_SZ;

    arraytopointer(req->read_blocks, config->num_carrier_blocks, carrier_blocks);

    if (map_entry_tuple[0].carrier_block_ptr == AFS_INVALID_BLOCK) {
        memset(req->data_block, 0, AFS_BLOCK_SIZE);
    } else {
        for (i = 0; i < config->num_carrier_blocks; i++) {
	    block_nums[i] = map_entry_tuple[i].carrier_block_ptr;
        }
        ret = read_pages((void **)carrier_blocks, req->bdev, block_nums, req->fs->data_start_off, false, config->num_carrier_blocks);
        afs_action(!ret, ret = -EIO, done, "could not read page at block [%u]", map_entry_tuple[i].carrier_block_ptr);
        

        // TODO: Read entropy blocks as well.
	//memcpy(req->data_block, req->read_blocks[0], AFS_BLOCK_SIZE);
	gfshare_ctx_dec_decode(share_decode, sharenrs, carrier_blocks, req->data_block);
	

        // Confirm hash matches.
        //digest = cityhash128_to_array(CityHash128(req->data_block, AFS_BLOCK_SIZE));
	//ret = memcmp(map_entry_hash, digest, SHA128_SZ);
	hash_sha1(req->data_block, AFS_BLOCK_SIZE, digest);
        ret = memcmp(map_entry_hash, digest + (SHA1_SZ - SHA128_SZ), SHA128_SZ);
        afs_action(!ret, ret = -ENOENT, done, "data block is corrupted [%u]", block);
    }
    ret = 0;

done:
    kfree(carrier_blocks);
    gfshare_ctx_free(share_decode);
    kfree(block_nums);
    return ret;
}

/**
 * Map a read request from userspace.
 */
int
afs_read_request(struct afs_map_request *req, struct bio *bio)
{
    struct bio_vec bv;
    struct bvec_iter iter;
    uint8_t *bio_data = NULL;
    uint32_t req_size;
    uint32_t block_num;
    uint32_t sector_offset;
    uint32_t segment_offset;
    int ret;

    block_num = (bio->bi_iter.bi_sector * AFS_SECTOR_SIZE) / AFS_BLOCK_SIZE;
    sector_offset = bio->bi_iter.bi_sector % (AFS_BLOCK_SIZE / AFS_SECTOR_SIZE);
    req_size = bio_sectors(bio) * AFS_SECTOR_SIZE;
    afs_action(req_size <= AFS_BLOCK_SIZE, ret = -EINVAL, done, "cannot handle requested size [%u]", req_size);
    // afs_debug("read request [Size: %u | Block: %u | Sector Off: %u]", req_size, block_num, sector_offset);

    // Read the raw block.
    ret = __afs_read_block(req, block_num);
    afs_assert(!ret, done, "could not read data block [%d:%u]", ret, block_num);

    // Copy back into the segments.
    segment_offset = 0;
    bio_for_each_segment (bv, bio, iter) {
        bio_data = kmap(bv.bv_page);
        if (bv.bv_len <= (req_size - segment_offset)) {
            memcpy(bio_data + bv.bv_offset, req->data_block + (sector_offset * AFS_SECTOR_SIZE) + segment_offset, bv.bv_len);
        } else {
            memcpy(bio_data + bv.bv_offset, req->data_block + (sector_offset * AFS_SECTOR_SIZE) + segment_offset, req_size - segment_offset);
            kunmap(bv.bv_page);
            break;
        }
        segment_offset += bv.bv_len;
        kunmap(bv.bv_page);
    }
    ret = 0;

done:
    return ret;
}

/**
 * Map a write request from userspace.
 */
int
afs_write_request(struct afs_map_request *req, struct bio *bio)
{
    struct afs_config *config = NULL;
    struct bio_vec bv;
    struct bvec_iter iter;
    struct afs_map_tuple *map_entry_tuple = NULL;
    uint8_t *map_entry = NULL;
    uint8_t *map_entry_hash = NULL;
    uint8_t *map_entry_entropy = NULL;
    uint8_t *bio_data = NULL;
    //uint8_t *digest;
    uint8_t digest[SHA1_SZ];
    uint32_t req_size;
    uint32_t block_num;
    uint32_t sector_offset;
    uint32_t segment_offset;
    bool modification = false;
    int ret = 0, i;

    uint8_t* sharenrs = "0123";
    gfshare_ctx *share_encode = NULL;

    uint8_t** carrier_blocks = NULL;
    uint32_t *block_nums = NULL;

    config = req->config;
    block_num = (bio->bi_iter.bi_sector * AFS_SECTOR_SIZE) / AFS_BLOCK_SIZE;
    sector_offset = bio->bi_iter.bi_sector % (AFS_BLOCK_SIZE / AFS_SECTOR_SIZE);
    req_size = bio_sectors(bio) * AFS_SECTOR_SIZE;
    
    carrier_blocks = kmalloc(sizeof(uint8_t*) * config->num_carrier_blocks, GFP_KERNEL);
    block_nums = kmalloc(sizeof(uint32_t) * config->num_carrier_blocks, GFP_KERNEL);

    afs_action(req_size <= AFS_BLOCK_SIZE, ret = -EINVAL, err, "cannot handle requested size [%u]", req_size);

    //TODO update this
    share_encode = gfshare_ctx_init_enc(sharenrs, config->num_carrier_blocks, 2, AFS_BLOCK_SIZE);

    map_entry = afs_get_map_entry(req->map, config, block_num);
    map_entry_tuple = (struct afs_map_tuple *)map_entry;
    map_entry_hash = map_entry + (config->num_carrier_blocks * sizeof(*map_entry_tuple));
    map_entry_entropy = map_entry_hash + SHA128_SZ;
    // afs_debug("write request [Size: %u | Block: %u | Sector Off: %u]", req_size, block_num, sector_offset);

    // If this write is a modification, then we perform a read-modify-write.
    // Otherwise, a new block is allocated and written to. We perform the
    // read of the block regardless because if the block is indeed unmapped, then
    // the data block will be simply zero'ed out.

    //ret = __afs_read_block(req, block_num);
    //afs_assert(!ret, err, "could not read data block [%d:%u]", ret, block_num);

    if (map_entry_tuple[0].carrier_block_ptr != AFS_INVALID_BLOCK) {
        modification = true;
    }

    // Copy from the segments.
    // NOTE: We don't technically need this complicated way
    // of copying, because write bio's are a single contiguous
    // page (because writes are cloned bio's). However, it does
    // not hurt to keep it since it doesn't result in a performance
    // degradation, and may help in future optimizations where we
    // may have multiple pages in a write bio.
    //
    // NOTE: ^This case is only true if the __clone_bio function
    // in dm_afs.c has override disabled. As of 3rd March 2019, the
    // override has been enabled due to massive lock contention.

    segment_offset = 0;
    bio_for_each_segment (bv, bio, iter) {
        bio_data = kmap(bv.bv_page);
        if (bv.bv_len <= (req_size - segment_offset)) {
            memcpy(req->data_block + (sector_offset * AFS_SECTOR_SIZE) + segment_offset, bio_data + bv.bv_offset, bv.bv_len);
        } else {
            memcpy(req->data_block + (sector_offset * AFS_SECTOR_SIZE) + segment_offset, bio_data + bv.bv_offset, req_size - segment_offset);
            kunmap(bv.bv_page);
            break;
        }
        segment_offset += bv.bv_len;
        kunmap(bv.bv_page);
    }

    // TODO: Read entropy blocks as well., if needed with secret sharing
    arraytopointer(req->write_blocks, config->num_carrier_blocks, carrier_blocks);
    gfshare_ctx_enc_getshares(share_encode, req->data_block, carrier_blocks);

    // Issue the writes.
    for (i = 0; i < config->num_carrier_blocks; i++) {
        // Allocate new block, or use old one.
        block_num = (modification) ? map_entry_tuple[i].carrier_block_ptr : acquire_block(req->fs, req->vector);
        afs_action(block_num != AFS_INVALID_BLOCK, ret = -ENOSPC, reset_entry, "no free space left");
        map_entry_tuple[i].carrier_block_ptr = block_num;
	block_nums[i] = block_num;
    }
    //ret = write_page(req->data_block, req->bdev, block_num, req->fs->data_start_off, false);
    ret = write_pages((const void **)carrier_blocks, req->bdev, block_nums, req->fs->data_start_off, false, config->num_carrier_blocks);
    afs_action(!ret, ret = -EIO, reset_entry, "could not write page at block [%u]", block_num);
    

    // TODO: Set the entropy hash correctly, may not be needed
    digest = cityhash128_to_array(CityHash128(req->data_block, AFS_BLOCK_SIZE));
    memcpy(map_entry_hash, digest, SHA128_SZ);
    //hash_sha1(req->data_block, AFS_BLOCK_SIZE, digest);
    //memcpy(map_entry_hash, digest + (SHA1_SZ - SHA128_SZ), SHA128_SZ);
    memset(map_entry_entropy, 0, ENTROPY_HASH_SZ);

    kfree(carrier_blocks);
    kfree(block_nums);
    gfshare_ctx_free(share_encode);
    return ret;

reset_entry:
    for (i = 0; i < config->num_carrier_blocks; i++) {
        if (map_entry_tuple[i].carrier_block_ptr != AFS_INVALID_BLOCK) {
            allocation_free(req->vector, map_entry_tuple[i].carrier_block_ptr);
        }
        map_entry_tuple[i].carrier_block_ptr = AFS_INVALID_BLOCK;
    }

err:
    kfree(carrier_blocks);
    kfree(block_nums);
    gfshare_ctx_free(share_encode);
    return ret;
}
