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
#include "lib/aont.h"

#define CONTAINER_OF(MemberPtr, StrucType, MemberName) ((StrucType*)( (char*)(MemberPtr) - offsetof(StrucType, MemberName)))

/**
 * Initialize an engine queue.
 */
void
afs_eq_init(struct afs_engine_queue *eq) {
    eq->mq_tree = RB_ROOT;
    spin_lock_init(&eq->mq_lock);
}

/**
 * Add a map element to a map queue.
 */
void
afs_eq_add(struct afs_engine_queue *eq, struct afs_map_request *element) {
    struct rb_node **new = &(eq->mq_tree.rb_node), *parent = NULL;
    
    spin_lock_irq(&eq->mq_lock);
    while (*new) {
        struct afs_map_request *this = container_of(*new, struct afs_map_request, node);
        
        parent = *new;
        if (element->block < this->block) {
            new = &(*new)->rb_left;
        } else if (element->block > this->block) {
            new = &(*new)->rb_right;
        } else {
            afs_debug("could not insert into tree");       
        }
    }
    // add new node and rebalance tree
    rb_link_node(&element->node, parent, new);
    rb_insert_color(&element->node, &eq->mq_tree);
    spin_unlock_irq(&eq->mq_lock);
}

/**
 * Check if an engine queue is empty without locking.
 */
bool
afs_eq_empty_unsafe(struct afs_engine_queue *eq) {
    if (RB_EMPTY_ROOT(&eq->mq_tree)) {
        return true;
    } else {
        return false;
    }
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
 * Remove a specified request from the red/black tree.
 */
void inline afs_eq_remove(struct afs_engine_queue *eq, struct afs_map_request *req) {
    spin_lock(&eq->mq_lock);
    if(!afs_eq_empty_unsafe(eq)){
        rb_erase(&req->node, &eq->mq_tree);
    }
    spin_unlock(&eq->mq_lock);
}

/**
 * Check if an engine queue contains a request with a specified
 * bio.
 */
struct afs_map_request *
afs_eq_req_exist(struct afs_engine_queue *eq, struct bio *bio) {
    struct rb_node *node = eq->mq_tree.rb_node;
    uint32_t block_num = (bio->bi_iter.bi_sector * AFS_SECTOR_SIZE) / AFS_BLOCK_SIZE;

    spin_lock(&eq->mq_lock);
    while (node) {
        struct afs_map_request *this = container_of(node, struct afs_map_request, node);

        if (block_num < this->block) {
            node = node->rb_left;
        } else if (block_num > this->block){
            node = node->rb_right;
        } else {
            spin_unlock(&eq->mq_lock);
            return this;
        }
    }
    spin_unlock(&eq->mq_lock);
    return NULL;
}

/**
 * Get a pointer to a map entry in the Artifice map.
 * TODO: Protect by a lock.
 */
static inline uint8_t *
afs_get_map_entry(uint8_t *map, struct afs_config *config, uint32_t index) {
    return map + (index * config->map_entry_sz);
}

/**
 * Cleanup a completed request.
 */
void 
afs_req_clean(struct afs_map_request *req) {
    int i;

    //set the state of the request to completed
    atomic64_set(&req->state, REQ_STATE_COMPLETED);

    for(i = 0; i < req->config->num_carrier_blocks; i++){
       free_page((uint64_t)req->carrier_blocks[i]);
    }
    
    /*if (bio_op(req->bio) == REQ_OP_WRITE) {
        afs_eq_remove(req->eq, req);
    }*/
  
    //end the virtual block device's recieved bio
    if(req->bio) {
        bio_endio(req->bio);
    }

    if (req->encoder != NULL){
        gfshare_ctx_free(req->encoder);
        req->encoder = NULL;   
    }
    if (req->allocated_write_page) {
        kfree(req->allocated_write_page);
        req->allocated_write_page = NULL;
    }
    
    kfree(req);
    req = NULL; 
}

/**
 * Custom end_io function to signal completion of all bio operations in a batch
 */
static void 
afs_read_endio(struct bio *bio) {
    struct afs_map_request *req = bio->bi_private;
    //uint8_t *digest;
    //int ret = 0;
    struct bio_vec bv;
    struct bvec_iter iter;
    uint8_t *bio_data = NULL;
    uint32_t segment_offset;
    uint32_t i;
    uint16_t checksum;
    //TODO change these two to reflect the erasures

    bio_put(bio);
 
    if(atomic_dec_and_test(&req->bios_pending)) {
	//TODO: only do this when running a repair process, it is kind of pointless to do with every read
	for(i = 0; i < req->config->num_carrier_blocks; i++) {
            checksum = cityhash32_to_16(req->carrier_blocks[i], AFS_BLOCK_SIZE);
	    if(memcmp(&req->map_entry_tuple[i].checksum, &checksum, sizeof(uint16_t))) { 
		afs_debug("corrupted block: %d,  carrier block: %d, stored checksum %d, checksum %d, carrier block location %d", req->block, i, req->map_entry_tuple[i].checksum, checksum, req->map_entry_tuple[i].carrier_block_ptr);
                atomic_set(&req->rebuild_flag, 1);
		req->erasures[i] = '0';
	    }
	}

        //memcpy(req->data_block, req->carrier_blocks[0], AFS_BLOCK_SIZE);
	if (req->encoding_type == SHAMIR) {
	    gfshare_ctx_dec_decode(req->encoder, req->erasures, req->carrier_blocks, req->data_block);
	} else if (req->encoding_type == AONT_RS) {
	    decode_aont_package(req->map_entry_difference, req->data_block, AFS_BLOCK_SIZE, req->carrier_blocks, req->iv, 2, req->config->num_carrier_blocks - 2, req->erasures, req->num_erasures);
	}
	
        //Confirm hash matches.
        //digest = cityhash128_to_array(CityHash128(req->data_block, AFS_BLOCK_SIZE));
        //ret = memcmp(req->map_entry_hash, digest, SHA128_SZ);
        //TODO only run this check when explicitly rebuilding, while mounted it is kind of useless
        //afs_action(!ret, ret = -ENOENT, err, "data block is corrupted [%u]", req->block);
                
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
        afs_req_clean(req);
    }
    return;
}

static void 
afs_write_endio(struct bio *bio) {
    struct afs_map_request *req = bio->bi_private;
    uint8_t *digest;
    uint16_t checksum;
    uint32_t i;

    bio_put(bio); 
    if(atomic_dec_and_test(&req->bios_pending)) {
        digest = cityhash128_to_array(CityHash128(req->data_block, AFS_BLOCK_SIZE));
        //memcpy(req->map_entry_hash, digest, SHA128_SZ);
        //memset(req->map_entry_entropy, 0, ENTROPY_HASH_SZ);
        for(i = 0; i < req->config->num_carrier_blocks; i++) {
            checksum = cityhash32_to_16(req->carrier_blocks[i], AFS_BLOCK_SIZE); 
            memcpy(&req->map_entry_tuple[i].checksum, &checksum, sizeof(uint16_t));
            req->map_entry_tuple[i].checksum = cityhash32_to_16(req->carrier_blocks[i], AFS_BLOCK_SIZE);
	}

        afs_req_clean(req);
    }
    return;
}

static int
read_pages(struct afs_map_request *req, bool used_vmalloc, uint32_t num_pages) {
    uint64_t sector_num;
    const int page_offset = 0;
    int i, ret = 0;
    struct bio **read_bios = NULL;
    
    read_bios = kmalloc(sizeof(struct bio *) * num_pages, GFP_KERNEL);
    atomic_set(&req->bios_pending, num_pages);

    for(i = 0; i < num_pages; i++) {
	struct page *page_structure;

        read_bios[i] = bio_alloc(GFP_NOIO, 1);
        afs_action(!IS_ERR(read_bios[i]), ret = PTR_ERR(read_bios[i]), done, "could not allocate bio [%d]", ret);
        // Make sure page is aligned.
        afs_action(!((uint64_t)req->carrier_blocks[i] & (AFS_BLOCK_SIZE - 1)), ret = -EINVAL, done, "page is not aligned [%d]", ret);

        // Acquire page structure and sector offset.
        page_structure = (used_vmalloc) ? vmalloc_to_page(req->carrier_blocks[i]) : virt_to_page(req->carrier_blocks[i]);
        sector_num = (req->block_nums[i] * AFS_SECTORS_PER_BLOCK) + req->fs->data_start_off;

        read_bios[i]->bi_opf |= REQ_OP_READ;
        bio_set_dev(read_bios[i], req->bdev);
        read_bios[i]->bi_iter.bi_sector = sector_num;
        bio_add_page(read_bios[i], page_structure, AFS_BLOCK_SIZE, page_offset);
        read_bios[i]->bi_private = req;
        read_bios[i]->bi_end_io = afs_read_endio;
        generic_make_request(read_bios[i]);
    }
done:
    for(i = 0; i < num_pages; i++) read_bios[i] = NULL;
    kfree(read_bios);
    return ret;
}


static int
write_pages(struct afs_map_request *req, bool used_vmalloc, uint32_t num_pages) {
    uint64_t sector_num;
    int i, ret = 0;
    const int page_offset = 0;
    struct bio **write_bios = NULL;
   
    write_bios = kmalloc(sizeof(struct bio *) * num_pages, GFP_KERNEL);
    atomic_set(&req->bios_pending, num_pages);

    for(i = 0; i < num_pages; i++) {
        struct page *page_structure;

        write_bios[i] = bio_alloc(GFP_NOIO, 1);
        afs_action(!IS_ERR(write_bios[i]), ret = PTR_ERR(write_bios[i]), done, "could not allocate bio [%d]", ret);

        // Make sure page is aligned.
        afs_action(!((uint64_t)req->carrier_blocks[i] & (AFS_BLOCK_SIZE - 1)), ret = -EINVAL, done, "page is not aligned [%d]", ret);

        // Acquire page structure and sector offset.
        page_structure = (used_vmalloc) ? vmalloc_to_page(req->carrier_blocks[i]) : virt_to_page(req->carrier_blocks[i]);
        sector_num = (req->block_nums[i] * AFS_SECTORS_PER_BLOCK) + req->fs->data_start_off;

        write_bios[i]->bi_opf |= REQ_OP_WRITE;
        bio_set_dev(write_bios[i], req->bdev);
        write_bios[i]->bi_iter.bi_sector = sector_num;
        bio_add_page(write_bios[i], page_structure, AFS_BLOCK_SIZE, page_offset);
        write_bios[i]->bi_private = req;
        write_bios[i]->bi_end_io = afs_write_endio;
        generic_make_request(write_bios[i]);
    }
done:
    for(i = 0; i < num_pages; i++) write_bios[i] = NULL;
    kfree(write_bios);
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

    for(i = 0; i < config->num_carrier_blocks; i++) {
        req->erasures[i] = i + '0';
    }

    if(req->encoding_type == SHAMIR){
        req->encoder = gfshare_ctx_init_enc(req->erasures, config->num_carrier_blocks, 2, AFS_BLOCK_SIZE);
        gfshare_ctx_enc_getshares(req->encoder, req->data_block, req->carrier_blocks);
    }else if(req->encoding_type == AONT_RS){
        encode_aont_package(req->map_entry_difference, req->data_block, AFS_BLOCK_SIZE, req->carrier_blocks, req->iv, 2, config->num_carrier_blocks - 2);
    }

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
    if(req->encoding_type == SHAMIR){
        gfshare_ctx_free(req->encoder);
        req->encoder = NULL;
    }
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
    int i = 0;

    afs_action(req != NULL && bio != NULL, ret = -EIO, done, "already freed request");
    afs_action(atomic64_read(&req->state) == REQ_STATE_FLIGHT, ret = -EINVAL, done, "Request already completed");

    //afs_debug("read request [Size: %u | Block: %u | Sector Off: %u]", req_size, req->block, sector_offset);


    req->map_entry = afs_get_map_entry(req->map, req->config, req->block);
    req->map_entry_tuple = (struct afs_map_tuple *)req->map_entry;
    req->map_entry_hash = req->map_entry + (req->config->num_carrier_blocks * sizeof(*req->map_entry_tuple));
    req->map_entry_difference = req->map_entry + (req->config->num_carrier_blocks * sizeof(*req->map_entry_tuple));
    //req->map_entry_entropy = req->map_entry_hash + CARRIER_HASH_SZ;

    //The block is unallocated, zero fill the data block, remap and return, clean up request
    if (req->map_entry_tuple[0].carrier_block_ptr == AFS_INVALID_BLOCK) {
        memset(req->data_block, 0, AFS_BLOCK_SIZE);
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
        afs_req_clean(req);
    } else {
	if (req->encoding_type == SHAMIR) {
            req->encoder = gfshare_ctx_init_dec(req->erasures, req->config->num_carrier_blocks, 2, AFS_BLOCK_SIZE);
        }

        for (i = 0; i < req->config->num_carrier_blocks; i++) {
            req->block_nums[i] = req->map_entry_tuple[i].carrier_block_ptr;
            req->erasures[i] = i + '0';
        }
        ret = read_pages(req, false, req->config->num_carrier_blocks);
        afs_action(!ret, ret = -EIO, done, "could not read page at block [%u]", req->map_entry_tuple[i].carrier_block_ptr);
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

    afs_action(atomic64_read(&req->state) == REQ_STATE_FLIGHT, ret = -EINVAL, err, "Request already completed");

    config = req->config;

    req->map_entry = afs_get_map_entry(req->map, config, req->block);
    req->map_entry_tuple = (struct afs_map_tuple *)req->map_entry;
    //TODO the hash is specific to the secret sharing version
    req->map_entry_hash = req->map_entry + (config->num_carrier_blocks * sizeof(*req->map_entry_tuple));
    req->map_entry_difference = req->map_entry + (config->num_carrier_blocks * sizeof(*req->map_entry_tuple));
    req->map_entry_entropy = req->map_entry_hash + CARRIER_HASH_SZ;
    //afs_debug("write request [Size: %u | Block: %u | Sector Off: %u]", req_size, block_num, sector_offset);


    // If this write is a modification, then we perform a read-modify-write.
    // Otherwise, a new block is allocated and written to. We perform the
    // read of the block regardless because if the block is indeed unmapped, then
    // the data block will be simply zero'ed out.

    if (req->map_entry_tuple[0].carrier_block_ptr != AFS_INVALID_BLOCK) {
        modification = true;
        //ret = __afs_read_block(req);
        afs_assert(!ret, err, "could not read data block [%d:%u]", ret, block_num);
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
    for(i = 0; i < config->num_carrier_blocks; i++){
        req->erasures[i] = i + '0';
    }

    //encode the block
    if(req->encoding_type == SHAMIR){
        req->encoder = gfshare_ctx_init_enc(req->erasures, config->num_carrier_blocks, 2, AFS_BLOCK_SIZE);
	gfshare_ctx_enc_getshares(req->encoder, req->data_block, req->carrier_blocks);
    } else if (req->encoding_type == AONT_RS){
        encode_aont_package(req->map_entry_difference, req->data_block, AFS_BLOCK_SIZE, req->carrier_blocks, req->iv, 2, config->num_carrier_blocks - 2);
    }


    for (i = 0; i < config->num_carrier_blocks; i++) {
        // Allocate new block, or use old one.
        //allocation_free(req->vector, req->map_entry_tuple[i].carrier_block_ptr);
        block_num = (modification) ? req->map_entry_tuple[i].carrier_block_ptr : acquire_block(req->fs, req->vector);
        afs_action(block_num != AFS_INVALID_BLOCK, ret = -ENOSPC, reset_entry, "no free space left");
        req->map_entry_tuple[i].carrier_block_ptr = block_num;
	req->block_nums[i] = block_num;
        //memcpy(req->carrier_blocks[i], req->data_block, AFS_BLOCK_SIZE);
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
    if (req->encoding_type == SHAMIR) {
        gfshare_ctx_free(req->encoder);
    }
    req->encoder = NULL;

err:
    return ret;
}
