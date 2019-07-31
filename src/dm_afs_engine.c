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

struct afs_bio_private{
    struct afs_map_request *req;
    atomic_t bios_pending;
    //struct bio **bio_list;
};

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

static void afs_req_clean(struct afs_map_request *req){

    //set the state of the request to completed
    atomic64_set(&req->state, REQ_STATE_COMPLETED);

    //end the virtual block device's recieved bio
    bio_endio(req->bio);

    //free any dynamically allocated objects in the request struct
    if(req->carrier_blocks){
        kfree(req->carrier_blocks);
    }
    if(req->block_nums){
        kfree(req->block_nums);
    }
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
static void afs_read_endio(struct bio *bio){
    struct afs_bio_private *ctx = bio->bi_private;
    struct afs_map_request *req = ctx->req;
    uint8_t *digest;
    struct afs_map_tuple *map_entry_tuple = NULL;
    uint8_t *map_entry = NULL;
    uint8_t *map_entry_hash = NULL;
    uint8_t *map_entry_entropy = NULL;
    struct afs_map_queue *element = NULL;
    int ret;

    afs_debug("reached endio function");
    bio_put(bio);
 
    if(atomic_dec_and_test(&ctx->bios_pending)){
        //set up map entry stuff
        map_entry = afs_get_map_entry(req->map, req->config, req->block);
        map_entry_tuple = (struct afs_map_tuple *)map_entry;
        map_entry_hash = map_entry + (req->config->num_carrier_blocks * sizeof(*map_entry_tuple));
        map_entry_entropy = map_entry_hash + SHA128_SZ;

        // TODO: Read entropy blocks as well.
	    memcpy(req->data_block, req->read_blocks[0], AFS_BLOCK_SIZE);
	    //gfshare_ctx_dec_decode(req->encoder, req->sharenrs, req->carrier_blocks, req->data_block);
	
        // Confirm hash matches.
        digest = cityhash128_to_array(CityHash128(req->data_block, AFS_BLOCK_SIZE));
	    ret = memcmp(map_entry_hash, digest, SHA128_SZ);
        //hash_sha1(req->data_block, AFS_BLOCK_SIZE, digest);
        //ret = memcmp(map_entry_hash, digest + (SHA1_SZ - SHA128_SZ), SHA128_SZ);
        afs_action(!ret, ret = -ENOENT, err, "data block is corrupted [%u]", req->block);
        
        //cleanup
        afs_debug("Io function processed");
        //element = container_of(req, struct afs_map_queue, req);
        //afs_req_clean(req);
        //schedule_work(element->clean_ws);
        afs_debug("endio function cleaned up");
    }
    return;

err:
    //TODO create a data structure for remapping corrupted blocks
    afs_debug("Corrupted block");
}

static void afs_write_endio(struct bio *bio){
    struct afs_bio_private *ctx = bio->bi_private;
    struct afs_map_request *req = ctx->req;
    uint8_t *digest;
    struct afs_map_tuple *map_entry_tuple = NULL;
    uint8_t *map_entry = NULL;
    uint8_t *map_entry_hash = NULL;
    uint8_t *map_entry_entropy = NULL;
    struct afs_map_queue *element = NULL;

    afs_debug("reached endio function, bios_pending %d, context %p", atomic_read(&ctx->bios_pending), ctx);
    //bio_put(bio); 
    if(atomic_dec_and_test(&ctx->bios_pending)){
	afs_debug("started in endio function");
        map_entry = afs_get_map_entry(req->map, req->config, req->block);
        map_entry_tuple = (struct afs_map_tuple *)map_entry;
        map_entry_hash = map_entry + (req->config->num_carrier_blocks * sizeof(*map_entry_tuple));
        map_entry_entropy = map_entry_hash + SHA128_SZ;

	afs_debug("computing hash");
        // TODO: Set the entropy hash correctly, may not be needed
        digest = cityhash128_to_array(CityHash128(req->data_block, AFS_BLOCK_SIZE));
        memcpy(map_entry_hash, digest, SHA128_SZ);
        //hash_sha1(req->data_block, AFS_BLOCK_SIZE, digest);
        //memcpy(map_entry_hash, digest + (SHA1_SZ - SHA128_SZ), SHA128_SZ);
        memset(map_entry_entropy, 0, ENTROPY_HASH_SZ);
        
        afs_debug("write endio function processed");
        //cleanup
        //element = container_of(req, struct afs_map_queue, req);
        //afs_req_clean(req);
        //schedule_work(element->clean_ws);
        afs_debug("write endio function cleaned up");
    }
    return;
}

int
read_pages(struct afs_map_request *req, bool used_vmalloc, uint32_t num_pages){
    uint64_t sector_num;
    const int page_offset = 0;
    int i = 0;
    int ret = 0;
    struct bio **bio = NULL;
    struct afs_bio_private *completion = NULL;
    
    completion = kmalloc(sizeof(struct afs_bio_private), GFP_KERNEL);
    bio = kmalloc(sizeof(struct bio *) * num_pages, GFP_KERNEL);
    atomic_set(&completion->bios_pending, num_pages);
    completion->req = req;
    afs_debug("read bios ready to submit");
    for(i = 0; i < num_pages; i++){
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

        bio[i]->bi_private = completion;
        bio[i]->bi_end_io = afs_read_endio;
        afs_debug("submitting bio %d", i);
        //generic_make_request(bio[i]);
	//submit_bio(bio[i]);
        afs_debug("submitted bio %d", i);
    }
    afs_debug("All read bios submitted");

done:
    //kfree(bio);
    return ret;
}


int
write_pages(struct afs_map_request *req, void **carrier_blocks, bool used_vmalloc, uint32_t num_pages){
    uint64_t sector_num;
    int ret = 0;
    int i = 0;
    const int page_offset = 0;
    struct bio **bio = NULL;
    struct afs_bio_private *completion = NULL;
   
    completion = kmalloc(sizeof(struct afs_bio_private), GFP_KERNEL);
    bio = kmalloc(sizeof(struct bio *) * num_pages, GFP_KERNEL);
    atomic_set(&completion->bios_pending, num_pages);
    afs_debug("current value of atomic %d, completion %p", atomic_read(&completion->bios_pending), completion);
    completion->req = req;

    for(i = 0; i < num_pages; i++){
        struct page *page_structure;

        bio[i] = bio_alloc(GFP_NOIO, 1);
        afs_action(!IS_ERR(bio[i]), ret = PTR_ERR(bio[i]), done, "could not allocate bio [%d]", ret);

        // Make sure page is aligned.
        afs_action(!((uint64_t)carrier_blocks[i] & (AFS_BLOCK_SIZE - 1)), ret = -EINVAL, done, "page is not aligned [%d]", ret);

        // Acquire page structure and sector offset.
        page_structure = (used_vmalloc) ? vmalloc_to_page(carrier_blocks[i]) : virt_to_page(carrier_blocks[i]);
        sector_num = ((req->block_nums[i] * AFS_BLOCK_SIZE) / AFS_SECTOR_SIZE) + req->fs->data_start_off;

        afs_debug("pages set up");

        bio[i]->bi_opf |= REQ_OP_WRITE;
        bio_set_dev(bio[i], req->bdev);
        bio[i]->bi_iter.bi_sector = sector_num;
        bio_add_page(bio[i], page_structure, AFS_BLOCK_SIZE, page_offset);

        bio[i]->bi_private = completion;
        bio[i]->bi_end_io = afs_write_endio;
	afs_debug("Write set up bi_private %p", bio[i]->bi_private);
        generic_make_request(bio[i]);
    }
    afs_debug("All write bios submitted");

done:
    //kfree(bio);
    return ret;
}

/**
 * Read a block from the map.
 * In case a block is unmapped, zero-fill it.
 */
static int
__afs_read_block(struct afs_map_request *req)
{
    struct afs_config *config = NULL;
    struct afs_map_tuple *map_entry_tuple = NULL;
    uint8_t *map_entry = NULL;
    uint8_t *map_entry_hash = NULL;
    uint8_t *map_entry_entropy = NULL;
    int ret, i;

    config = req->config;
    //TODO change this when entropy handling is added
    //TODO needs to calculate sharenrs and adjust as needed

    //set up map entry stuff
    map_entry = afs_get_map_entry(req->map, config, req->block);
    map_entry_tuple = (struct afs_map_tuple *)map_entry;
    map_entry_hash = map_entry + (config->num_carrier_blocks * sizeof(*map_entry_tuple));
    map_entry_entropy = map_entry_hash + SHA128_SZ;

    if (map_entry_tuple[0].carrier_block_ptr == AFS_INVALID_BLOCK) {
        memset(req->data_block, 0, AFS_BLOCK_SIZE);
        req->pending = 0;
    } else {

        req->carrier_blocks = kmalloc(sizeof(uint8_t*)*config->num_carrier_blocks, GFP_KERNEL);
        req->block_nums = kmalloc(sizeof(uint32_t) * config->num_carrier_blocks, GFP_KERNEL);
        //req->encoder = gfshare_ctx_init_dec(sharenrs, config->num_carrier_blocks, 2, AFS_BLOCK_SIZE);
        //req->sharenrs = "0123";

        arraytopointer(req->read_blocks, config->num_carrier_blocks, req->carrier_blocks);

        for (i = 0; i < config->num_carrier_blocks; i++) {
            req->block_nums[i] = map_entry_tuple[i].carrier_block_ptr;
        }
        ret = read_pages(req, false, config->num_carrier_blocks);
        afs_action(!ret, ret = -EIO, done, "could not read page at block [%u]", map_entry_tuple[i].carrier_block_ptr);
    }
    ret = 0;
    return ret;

done:
    kfree(req->carrier_blocks);
    kfree(req->block_nums);
    //gfshare_ctx_free(req->encoder);
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
    uint32_t sector_offset;
    uint32_t segment_offset;
    int ret;

    req->block = (bio->bi_iter.bi_sector * AFS_SECTOR_SIZE) / AFS_BLOCK_SIZE;
    sector_offset = bio->bi_iter.bi_sector % (AFS_BLOCK_SIZE / AFS_SECTOR_SIZE);
    req_size = bio_sectors(bio) * AFS_SECTOR_SIZE;
    afs_action(req_size <= AFS_BLOCK_SIZE, ret = -EINVAL, done, "cannot handle requested size [%u]", req_size);
    //afs_debug("read request [Size: %u | Block: %u | Sector Off: %u]", req_size, req->block, sector_offset);

    // Read the raw block.
    ret = __afs_read_block(req);
    afs_assert(!ret, done, "could not read data block [%d:%u]", ret, req->block);

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
    //uint8_t digest[SHA1_SZ];
    uint32_t req_size;
    uint32_t block_num;
    uint32_t sector_offset;
    uint32_t segment_offset;
    bool modification = false;
    int ret = 0, i;

    config = req->config;
    req->block = (bio->bi_iter.bi_sector * AFS_SECTOR_SIZE) / AFS_BLOCK_SIZE;
    sector_offset = bio->bi_iter.bi_sector % (AFS_BLOCK_SIZE / AFS_SECTOR_SIZE);
    req_size = bio_sectors(bio) * AFS_SECTOR_SIZE;
    
    afs_action(req_size <= AFS_BLOCK_SIZE, ret = -EINVAL, err, "cannot handle requested size [%u]", req_size);

    map_entry = afs_get_map_entry(req->map, config, req->block);
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
  
    req->carrier_blocks = kmalloc(sizeof(uint8_t*) * config->num_carrier_blocks, GFP_KERNEL);
    req->block_nums = kmalloc(sizeof(uint32_t) * config->num_carrier_blocks, GFP_KERNEL);
    //TODO update this
    //req->encoder = gfshare_ctx_init_enc(sharenrs, config->num_carrier_blocks, 2, AFS_BLOCK_SIZE);
    //req->sharenrs = "0123";

    // TODO: Read entropy blocks as well., if needed with secret sharing
    arraytopointer(req->write_blocks, config->num_carrier_blocks, req->carrier_blocks);
    //gfshare_ctx_enc_getshares(req->encoder, req->data_block, req->carrier_blocks);

    // Issue the writes.
    for (i = 0; i < config->num_carrier_blocks; i++) {
        // Allocate new block, or use old one.
        block_num = (modification) ? map_entry_tuple[i].carrier_block_ptr : acquire_block(req->fs, req->vector);
        afs_action(block_num != AFS_INVALID_BLOCK, ret = -ENOSPC, reset_entry, "no free space left");
        map_entry_tuple[i].carrier_block_ptr = block_num;
	    req->block_nums[i] = block_num;
        memcpy(req->carrier_blocks[i], req->data_block, AFS_BLOCK_SIZE);
    }
    afs_debug("writing pages");
    ret = write_pages(req, (void**)req->carrier_blocks, false, config->num_carrier_blocks);
    afs_action(!ret, ret = -EIO, reset_entry, "could not write page at block [%u]", block_num);
    afs_debug("Write request processed");
    return ret;

reset_entry:
    for (i = 0; i < config->num_carrier_blocks; i++) {
        if (map_entry_tuple[i].carrier_block_ptr != AFS_INVALID_BLOCK) {
            allocation_free(req->vector, map_entry_tuple[i].carrier_block_ptr);
        }
        map_entry_tuple[i].carrier_block_ptr = AFS_INVALID_BLOCK;
    }
    kfree(req->carrier_blocks);
    kfree(req->block_nums);
    //gfshare_ctx_free(req->encoder);

err:
    return ret;
}
