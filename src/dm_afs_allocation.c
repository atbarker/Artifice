/**
 * Author: Yash Gupta <ygupta@ucsc.edu>, Austen Barker <atbarker@ucsc.edu>
 * Copyright: UC Santa Cruz, SSRC
 */
#include <dm_afs.h>
#include <dm_afs_modules.h>
#include <linux/random.h>


/**
 * Pick an index at random within the allocation vector
 */
uint32_t random_block_index(struct afs_passive_fs *fs, struct afs_allocation_vector *vector){
    uint32_t block_num;
    get_random_bytes(&block_num, sizeof(uint32_t));
    block_num = block_num % fs->list_len;
    return block_num; 
}

/**
 * Get the state of a block in the allocation vector.
 */
uint8_t
allocation_get(struct afs_allocation_vector *vector, uint32_t index)
{
    int ret = bit_vector_get(vector->vector, index);

    // Make sure return code was valid.
    if (ret < 0) {
        afs_alert("bit_vector_get returned %d", ret);
        ret = 0;
    }

    return (ret) ? 1 : 0;
}

/**
 * Set the usage of a block in the allocation vector.
 */
bool
allocation_set(struct afs_allocation_vector *vector, uint32_t index)
{
    int ret;

    // Make sure index is not already taken.
    if (allocation_get(vector, index)) {
        return false;
    }
    ret = bit_vector_set(vector->vector, index);

    // Make sure return code was valid.
    afs_assert(!ret, err, "bit_vector_set returned %d", ret);
    return true;

err:
    return false;
}

/**
 * Clear the usage of a block in the allocation vector.
 */
void
allocation_free(struct afs_allocation_vector *vector, uint32_t index)
{
    int ret = bit_vector_clear(vector->vector, index);

    // Make sure return code was valid.
    if (ret) {
        afs_alert("bit_vector_clear returned %d", ret);
    }
}

/**
 * Acquire a free block from the free list.
 * TODO just have it randomly select a block
 */
uint32_t
acquire_block(struct afs_passive_fs *fs, struct afs_allocation_vector *vector)
{
    static uint32_t block_num = 0;
    uint32_t current_num = 0;
    uint32_t ret;

    spin_lock(&vector->lock);
    //block_num = random_block_index(fs, vector);
    current_num = block_num;
    /*do {
        if (allocation_set(vector, block_num)) {
            ret = fs->block_list[block_num];
            spin_unlock(&vector->lock);
            return ret;
        }
	block_num = random_block_index(fs, vector);
	current_num++;
    } while (current_num != fs->list_len);
    spin_unlock(&vector->lock);
    */
    // Old sequential allocation
    spin_lock(&vector->lock);
    do {
        if (allocation_set(vector, block_num)) {
            ret = fs->block_list[block_num];
	    block_num = (block_num + 1) % fs->list_len;
            spin_unlock(&vector->lock);
            return ret;
        }
        block_num = (block_num + 1) % fs->list_len;
    } while (block_num != current_num);
    spin_unlock(&vector->lock);
    
    return AFS_INVALID_BLOCK;
}
