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
#include "lib/libgfshare.h"
#include "lib/city.h"

#define CONTAINER_OF(MemberPtr, StrucType, MemberName) ((StrucType*)( (char*)(MemberPtr) - offsetof(StrucType, MemberName)))

struct afs_bio_private {
    struct afs_map_request *req;
    atomic_t bios_pending;
};

/**
 * Convert 2 dimensional static array to double pointer 2d array.
 * TODO this can be thrown out once we get map request block arrays as double pointers instead of static arrays
 */
static inline void 
arraytopointer(uint8_t array[][AFS_BLOCK_SIZE], int size, uint8_t* output[AFS_BLOCK_SIZE]) {
    int i;
    for(i = 0; i < size; i++) {
        output[i] = array[i];
    }
}

/**
 * Initialize an engine queue.
 */
void
afs_eq_init(struct afs_engine_queue *eq) {
    INIT_LIST_HEAD(&eq->mq.list);
    spin_lock_init(&eq->mq_lock);
}

/**
 * Add a map element to a map queue.
 */
void
afs_eq_add(struct afs_engine_queue *eq, struct afs_map_queue *element) {
    spin_lock(&eq->mq_lock);
    list_add_tail(&element->list, &eq->mq.list);
    spin_unlock(&eq->mq_lock);
}

/**
 * Check if an engine queue is empty without locking.
 */
bool
afs_eq_empty_unsafe(struct afs_engine_queue *eq) {
    return list_empty(&eq->mq.list);
}

/**
 * Check if an engine queue is empty.
 */
bool
afs_eq_empty(struct afs_engine_queue *eq) {
    bool ret = false;

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
afs_eq_req_exist(struct afs_engine_queue *eq, struct bio *bio) {
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
afs_get_map_entry(uint8_t *map, struct afs_config *config, uint32_t index) {
    return map + (index * config->map_entry_sz);
}

static void 
afs_req_clean(struct afs_map_request *req) {
    //set the state of the request to completed
    atomic64_set(&req->state, REQ_STATE_COMPLETED);

    if(req->carrier_blocks != NULL) {
       kfree(req->carrier_blocks);
    }
    
    //end the virtual block device's recieved bio
    bio_endio(req->bio);

    //TODO figure out a safer cleanup option
    //re-enable when we want libgfshare
    //gfshare_ctx_free(req->encoder);   
    if (req->allocated_write_page) {
        kfree(req->allocated_write_page);
    } 
}

/**
 * Custom end_io function to signal completion of all bio operations in a batch
 */
static void 
afs_read_endio(struct bio *bio) {
    struct afs_bio_private *ctx = bio->bi_private;
    struct afs_map_request *req = ctx->req;
    uint8_t *digest;
    struct afs_map_queue *element = NULL;
    int ret = 0, i;
    struct bio_vec bv;
    struct bvec_iter iter;
    uint8_t *bio_data = NULL;
    uint32_t segment_offset;
    uint16_t checksum;


    bio_put(bio);
 
    if(atomic_dec_and_test(&ctx->bios_pending)) {

	for(i = 0; i < req->config->num_carrier_blocks; i++) {
            checksum = cityhash32_to_16(req->carrier_blocks[i], AFS_BLOCK_SIZE);
	    if(memcmp(&req->map_entry_tuple[i].checksum, &checksum, sizeof(uint16_t))) { 
		afs_debug("corrupted block %d, stored checksum %d, checksum %d", i, req->map_entry_tuple[i].checksum, checksum);
                atomic_set(&req->rebuild_flag, 1);
		req->sharenrs[i] = '0';
	    }
	}

        // TODO: Read entropy blocks as well.
        memcpy(req->data_block, req->read_blocks[0], AFS_BLOCK_SIZE);
	//gfshare_ctx_dec_decode(req->encoder, req->sharenrs, req->carrier_blocks, req->data_block);
	
        // Confirm hash matches.
        digest = cityhash128_to_array(CityHash128(req->data_block, AFS_BLOCK_SIZE));
        ret = memcmp(req->map_entry_hash, digest, SHA128_SZ);
        afs_action(!ret, ret = -ENOENT, err, "data block is corrupted [%u]", req->block);
                
	segment_offset = 0;
        bio_for_each_segment (bv, req->bio, iter) {
            bio_data = kmap(bv.bv_page);
            if (bv.bv_len <= (req->request_size - segment_offset)) {
                memcpy(bio_data + bv.bv_offset, req->data_block + (req->sector_offset * AFS_SECTOR_SIZE) + segment_offset, bv.bv_len);
            } else {
                memcpy(bio_data + bv.bv_offset, req->data_block + (req->sector_offset * AFS_SECTOR_SIZE) + segment_offset, req->request_size - segment_offset);
                kunmap(bv.bv_page);
                break;
            }
            segment_offset += bv.bv_len;
            kunmap(bv.bv_page);
        }
	if(atomic_read(&req->rebuild_flag)) {
            //write a new function called write blocks, should have a flag to remap blocks
	    //only after that is finished can we clean up the request so we return
	    rebuild_blocks(req);
	    return;
	}

        //cleanup
err:
        element = container_of(req, struct afs_map_queue, req);
        afs_req_clean(req);
	kfree(ctx);
        schedule_work(element->clean_ws);
    }
    return;
}

static void 
afs_write_endio(struct bio *bio) {
    struct afs_bio_private *ctx = bio->bi_private;
    struct afs_map_request *req = ctx->req;
    uint8_t *digest;
    struct afs_map_queue *element = NULL;
    int i;

    bio_put(bio); 
    if(atomic_dec_and_test(&ctx->bios_pending)) {
        // TODO: Set the entropy hash correctly, may not be needed
        digest = cityhash128_to_array(CityHash128(req->data_block, AFS_BLOCK_SIZE));
        memcpy(req->map_entry_hash, digest, SHA128_SZ);
        memset(req->map_entry_entropy, 0, ENTROPY_HASH_SZ);
        for(i = 0; i < req->config->num_carrier_blocks; i++) {
            req->map_entry_tuple[i].checksum = cityhash32_to_16(req->carrier_blocks[i], AFS_BLOCK_SIZE);
	}

        //cleanup
        element = container_of(req, struct afs_map_queue, req);
        afs_req_clean(req);
	kfree(ctx);
        schedule_work(element->clean_ws);
    }
    return;
}

static int
read_pages(struct afs_map_request *req, bool used_vmalloc, uint32_t num_pages) {
    uint64_t sector_num;
    const int page_offset = 0;
    int i, ret = 0;
    struct bio **bio = NULL;
    struct afs_bio_private *ctx = NULL;
    
    ctx = kmalloc(sizeof(struct afs_bio_private), GFP_KERNEL);
    bio = kmalloc(sizeof(struct bio *) * num_pages, GFP_KERNEL);
    atomic_set(&ctx->bios_pending, num_pages);
    ctx->req = req;

    for(i = 0; i < num_pages; i++) {
	struct page *page_structure;

        bio[i] = bio_alloc(GFP_NOIO, 1);
        afs_action(!IS_ERR(bio[i]), ret = PTR_ERR(bio[i]), done, "could not allocate bio [%d]", ret);
        // Make sure page is aligned.
        afs_action(!((uint64_t)req->carrier_blocks[i] & (AFS_BLOCK_SIZE - 1)), ret = -EINVAL, done, "page is not aligned [%d]", ret);

        // Acquire page structure and sector offset.
        page_structure = (used_vmalloc) ? vmalloc_to_page(req->carrier_blocks[i]) : virt_to_page(req->carrier_blocks[i]);
        sector_num = ((req->block_nums[i] * AFS_BLOCK_SIZE) / AFS_SECTOR_SIZE) + req->fs->data_start_off;

        bio[i]->bi_opf |= REQ_OP_READ;
        bio_set_dev(bio[i], req->bdev);
        bio[i]->bi_iter.bi_sector = sector_num;
        bio_add_page(bio[i], page_structure, AFS_BLOCK_SIZE, page_offset);
        bio[i]->bi_private = ctx;
        bio[i]->bi_end_io = afs_read_endio;
        generic_make_request(bio[i]);
    }
    kfree(bio);
    return ret;

done:
    kfree(bio);
    kfree(ctx);
    return ret;
}


static int
write_pages(struct afs_map_request *req, bool used_vmalloc, uint32_t num_pages) {
    uint64_t sector_num;
    int i, ret = 0;
    const int page_offset = 0;
    struct bio **bio = NULL;
    struct afs_bio_private *ctx = NULL;
   
    ctx = kmalloc(sizeof(struct afs_bio_private), GFP_KERNEL);
    bio = kmalloc(sizeof(struct bio *) * num_pages, GFP_KERNEL);
    atomic_set(&ctx->bios_pending, num_pages);
    ctx->req = req;

    for(i = 0; i < num_pages; i++) {
        struct page *page_structure;

        bio[i] = bio_alloc(GFP_NOIO, 1);
        afs_action(!IS_ERR(bio[i]), ret = PTR_ERR(bio[i]), done, "could not allocate bio [%d]", ret);

        // Make sure page is aligned.
        afs_action(!((uint64_t)req->carrier_blocks[i] & (AFS_BLOCK_SIZE - 1)), ret = -EINVAL, done, "page is not aligned [%d]", ret);

        // Acquire page structure and sector offset.
        page_structure = (used_vmalloc) ? vmalloc_to_page(req->carrier_blocks[i]) : virt_to_page(req->carrier_blocks[i]);
        sector_num = ((req->block_nums[i] * AFS_BLOCK_SIZE) / AFS_SECTOR_SIZE) + req->fs->data_start_off;

        bio[i]->bi_opf |= REQ_OP_WRITE;
        bio_set_dev(bio[i], req->bdev);
        bio[i]->bi_iter.bi_sector = sector_num;
        bio_add_page(bio[i], page_structure, AFS_BLOCK_SIZE, page_offset);

        bio[i]->bi_private = ctx;
        bio[i]->bi_end_io = afs_write_endio;
        generic_make_request(bio[i]);
    }
    kfree(bio);
    return ret;

done:
    kfree(bio);
    kfree(ctx);
    return ret;
}

/**
 * Rebuilds and remaps a block that has already been read and reconstructed.
 *
 */
int 
rebuild_blocks(struct afs_map_request *req) {
    struct afs_config *config = NULL;
    int ret= 0, i;
    uint32_t block_num;

    //TODO, make sure this doesn't cause a null pointer error
    //for(i = 0; i < config->num_carrier_blocks; i++) {
    //    char sharenr[2];
    //    snprintf(sharenr, 1, "%d", i);
    //    req->sharenrs[i] = sharenr[0];
    //}

    //req->encoder = gfshare_ctx_init_enc(req->sharenrs, config->num_carrier_blocks, 2, AFS_BLOCK_SIZE);
    // TODO: Read entropy blocks as well., if needed with secret sharing
    arraytopointer(req->write_blocks, config->num_carrier_blocks, req->carrier_blocks);
    //gfshare_ctx_enc_getshares(req->encoder, req->data_block, req->carrier_blocks);

    for (i = 0; i < config->num_carrier_blocks; i++) {
        // Allocate new block, or use old one.
        block_num = acquire_block(req->fs, req->vector);
        afs_action(block_num != AFS_INVALID_BLOCK, ret = -ENOSPC, reset_entry, "no free space left");
        req->map_entry_tuple[i].carrier_block_ptr = block_num;
        req->block_nums[i] = block_num;
        memcpy(req->carrier_blocks[i], req->data_block, AFS_BLOCK_SIZE);
    }
    ret = write_pages(req, false, config->num_carrier_blocks);
    afs_action(!ret, ret = -EIO, reset_entry, "could not write page at block [%u]", block_num);
    return ret;

reset_entry:
    for (i = 0; i < config->num_carrier_blocks; i++) {
        if (req->map_entry_tuple[i].carrier_block_ptr != AFS_INVALID_BLOCK) {
            allocation_free(req->vector, req->map_entry_tuple[i].carrier_block_ptr);
        }
        req->map_entry_tuple[i].carrier_block_ptr = AFS_INVALID_BLOCK;
    }
    //gfshare_ctx_free(req->encoder);
    return ret;
}

/**
 * Read a block from the map.
 * In case a block is unmapped, zero-fill it.
 */
static int
__afs_read_block(struct afs_map_request *req) {
    struct afs_config *config = NULL;
    int ret = 0, i;

    config = req->config;

    //set up map entry stuff
    req->map_entry = afs_get_map_entry(req->map, config, req->block);
    req->map_entry_tuple = (struct afs_map_tuple *)req->map_entry;
    req->map_entry_hash = req->map_entry + (config->num_carrier_blocks * sizeof(*req->map_entry_tuple));
    req->map_entry_entropy = req->map_entry_hash + SHA128_SZ;

    if (req->map_entry_tuple[0].carrier_block_ptr == AFS_INVALID_BLOCK) {
        memset(req->data_block, 0, AFS_BLOCK_SIZE);
        atomic_set(&req->pending, 2);
    } else {
        req->carrier_blocks = kmalloc(sizeof(uint8_t*)*config->num_carrier_blocks, GFP_KERNEL);
        //req->block_nums = kmalloc(sizeof(uint32_t) * config->num_carrier_blocks, GFP_KERNEL);
	req->sharenrs = "0123";
        //req->encoder = gfshare_ctx_init_dec(req->sharenrs, config->num_carrier_blocks, 2, AFS_BLOCK_SIZE);
        arraytopointer(req->read_blocks, config->num_carrier_blocks, req->carrier_blocks);

        for (i = 0; i < config->num_carrier_blocks; i++) {
            req->block_nums[i] = req->map_entry_tuple[i].carrier_block_ptr;
        }
        ret = read_pages(req, false, config->num_carrier_blocks);
        afs_action(!ret, ret = -EIO, done, "could not read page at block [%u]", req->map_entry_tuple[i].carrier_block_ptr);
    }
    ret = 0;
    return ret;

done:
    kfree(req->carrier_blocks);
    req->carrier_blocks = NULL;
    //gfshare_ctx_free(req->encoder);
    return ret;
}

/**
 * Map a read request from userspace.
 */
int
afs_read_request(struct afs_map_request *req, struct bio *bio) {
    struct bio_vec bv;
    struct bvec_iter iter;
    uint8_t *bio_data = NULL;
    uint32_t segment_offset;
    int ret = 0;

    req->block = (bio->bi_iter.bi_sector * AFS_SECTOR_SIZE) / AFS_BLOCK_SIZE;
    req->sector_offset = bio->bi_iter.bi_sector % (AFS_BLOCK_SIZE / AFS_SECTOR_SIZE);
    req->request_size = bio_sectors(bio) * AFS_SECTOR_SIZE;
    afs_action(req->request_size <= AFS_BLOCK_SIZE, ret = -EINVAL, done, "cannot handle requested size [%u]", req->request_size);
    //afs_debug("read request [Size: %u | Block: %u | Sector Off: %u]", req_size, req->block, sector_offset);

    ret = __afs_read_block(req);
    afs_assert(!ret, done, "could not read data block [%d:%u]", ret, req->block);

    // Copy back into the segments.
    if(atomic_read(&req->pending) == 2) {
        segment_offset = 0;
        bio_for_each_segment (bv, bio, iter) {
            bio_data = kmap(bv.bv_page);
            if (bv.bv_len <= (req->request_size - segment_offset)) {
                memcpy(bio_data + bv.bv_offset, req->data_block + (req->sector_offset * AFS_SECTOR_SIZE) + segment_offset, bv.bv_len);
            } else {
                memcpy(bio_data + bv.bv_offset, req->data_block + (req->sector_offset * AFS_SECTOR_SIZE) + segment_offset, req->request_size - segment_offset);
                kunmap(bv.bv_page);
                break;
            }
            segment_offset += bv.bv_len;
            kunmap(bv.bv_page);
        }
        ret = 0;
    }

done:
    return ret;
}

//TODO perform a check based on a remap or corrupt block flag to remap corrupted blocks on read
/**
 * Map a write request from userspace.
 */
int
afs_write_request(struct afs_map_request *req, struct bio *bio)
{
    struct afs_config *config = NULL;
    struct bio_vec bv;
    struct bvec_iter iter;
    uint8_t *bio_data = NULL;
    uint32_t block_num;
    uint32_t segment_offset;
    bool modification = false;
    int ret = 0, i;

    config = req->config;
    req->block = (bio->bi_iter.bi_sector * AFS_SECTOR_SIZE) / AFS_BLOCK_SIZE;
    req->sector_offset = bio->bi_iter.bi_sector % (AFS_BLOCK_SIZE / AFS_SECTOR_SIZE);
    req->request_size = bio_sectors(bio) * AFS_SECTOR_SIZE;
    
    afs_action(req->request_size <= AFS_BLOCK_SIZE, ret = -EINVAL, err, "cannot handle requested size [%u]", req->request_size);

    req->map_entry = afs_get_map_entry(req->map, config, req->block);
    req->map_entry_tuple = (struct afs_map_tuple *)req->map_entry;
    req->map_entry_hash = req->map_entry + (config->num_carrier_blocks * sizeof(*req->map_entry_tuple));
    req->map_entry_entropy = req->map_entry_hash + SHA128_SZ;
    // afs_debug("write request [Size: %u | Block: %u | Sector Off: %u]", req_size, block_num, sector_offset);

    // If this write is a modification, then we perform a read-modify-write.
    // Otherwise, a new block is allocated and written to. We perform the
    // read of the block regardless because if the block is indeed unmapped, then
    // the data block will be simply zero'ed out.

    //ret = __afs_read_block(req, block_num);
    //afs_assert(!ret, err, "could not read data block [%d:%u]", ret, block_num);

    if (req->map_entry_tuple[0].carrier_block_ptr != AFS_INVALID_BLOCK) {
        modification = true;
    }

    req->carrier_blocks = kmalloc(sizeof(uint8_t*) * config->num_carrier_blocks, GFP_KERNEL);
    afs_action(req->carrier_blocks, ret = -ENOMEM, err, "could not allocate carrier block array [%d]", ret);

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
        if (bv.bv_len <= (req->request_size - segment_offset)) {
            memcpy(req->data_block + (req->sector_offset * AFS_SECTOR_SIZE) + segment_offset, bio_data + bv.bv_offset, bv.bv_len);
        } else {
            memcpy(req->data_block + (req->sector_offset * AFS_SECTOR_SIZE) + segment_offset, bio_data + bv.bv_offset, req->request_size - segment_offset);
            kunmap(bv.bv_page);
            break;
        }
        segment_offset += bv.bv_len;
        kunmap(bv.bv_page);
    }
    //TODO update this to better reflect the total number of carrier blocks
    /*for(i = 0; i < config->num_carrier_blocks; i++){
	char sharenr[2];
	snprintf(sharenr, 1, "%d", i);
        req->sharenrs[i] = sharenr[0];
    }*/
    //req->encoder = gfshare_ctx_init_enc(req->sharenrs, config->num_carrier_blocks, 2, AFS_BLOCK_SIZE);

    // TODO: Read entropy blocks as well., if needed with secret sharing
    arraytopointer(req->write_blocks, config->num_carrier_blocks, req->carrier_blocks);
    //gfshare_ctx_enc_getshares(req->encoder, req->data_block, req->carrier_blocks);

    for (i = 0; i < config->num_carrier_blocks; i++) {
        // Allocate new block, or use old one.
        block_num = (modification) ? req->map_entry_tuple[i].carrier_block_ptr : acquire_block(req->fs, req->vector);
        afs_action(block_num != AFS_INVALID_BLOCK, ret = -ENOSPC, reset_entry, "no free space left");
        req->map_entry_tuple[i].carrier_block_ptr = block_num;
	req->block_nums[i] = block_num;
        memcpy(req->carrier_blocks[i], req->data_block, AFS_BLOCK_SIZE);
    }
    ret = write_pages(req, false, config->num_carrier_blocks);
    afs_action(!ret, ret = -EIO, reset_entry, "could not write page at block [%u]", block_num);
    return ret;

reset_entry:
    for (i = 0; i < config->num_carrier_blocks; i++) {
        if (req->map_entry_tuple[i].carrier_block_ptr != AFS_INVALID_BLOCK) {
            allocation_free(req->vector, req->map_entry_tuple[i].carrier_block_ptr);
        }
        req->map_entry_tuple[i].carrier_block_ptr = AFS_INVALID_BLOCK;
    }
    //gfshare_ctx_free(req->encoder);

err:
    return ret;
}
