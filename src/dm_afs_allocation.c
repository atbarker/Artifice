/**
 * Author: Yash Gupta <ygupta@ucsc.edu>, Austen Barker <atbarker@ucsc.edu>
 * Copyright: UC Santa Cruz, SSRC
 */
#include <dm_afs.h>
#include <dm_afs_modules.h>

/**
 * Get the state of a block in the allocation vector.
 */
uint8_t
allocation_get(struct afs_private *context, uint32_t index)
{
    int ret = bit_vector_get(context->allocation_vec, index);

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
allocation_set(struct afs_private *context, uint32_t index)
{
    int ret;

    // Make sure index is not already taken.
    if (allocation_get(context, index)) {
        return false;
    }
    ret = bit_vector_set(context->allocation_vec, index);

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
allocation_free(struct afs_private *context, uint32_t index)
{
    int ret = bit_vector_clear(context->allocation_vec, index);

    // Make sure return code was valid.
    if (ret) {
        afs_alert("bit_vector_clear returned %d", ret);
    }
}

/**
 * Acquire a free block from the free list.
 */
uint32_t
acquire_block(struct afs_passive_fs *fs, struct afs_private *context)
{
    static uint32_t block_num = 0;
    uint32_t current_num = block_num;

    spin_lock(&context->allocation_lock);
    do {
        if (allocation_set(context, block_num)) {
            block_num = (block_num + 1) % fs->list_len;
            spin_unlock(&context->allocation_lock);
            return fs->block_list[block_num];
        }
        block_num = (block_num + 1) % fs->list_len;
    } while (block_num != current_num);
    spin_unlock(&context->allocation_lock);

    return AFS_INVALID_BLOCK;
}