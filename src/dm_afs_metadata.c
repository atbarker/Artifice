/**
 * Author: Yash Gupta <ygupta@ucsc.edu>, Austen Barker <atbarker@ucsc.edu>
 * Copyright: UC Santa Cruz, SSRC
 */
#include <crypto/hash.h>
#include <dm_afs.h>
#include <dm_afs_io.h>
#include <dm_afs_modules.h>
#include <linux/crypto.h>
#include <linux/errno.h>
#include <linux/random.h>

/**
 * Binary search for use in finding a superblock
 * returns block index in the free block list if true
 * returns -1 if false
 */
static int64_t binary_search(uint32_t *array, uint32_t value, uint32_t length){
    int first, last, middle;
    int64_t index = -1;

    first = 0;
    last = length - 1;
    middle = (first + last) / 2;

    while (first <= last){
        if(array[middle] < value){
            first = middle + 1;
        } else if (array[middle] == value){
            index = middle;
            break;
        } else {
            last = middle - 1;
        }
        middle = (first + last)/2;
    }

    return index;
}


/**
 * Build the configuration for an instance.
 * TODO build config for everything
 */
void
build_configuration(struct afs_private *context, uint8_t num_carrier_blocks, uint8_t num_entropy_blocks)
{
    struct afs_config *config = &context->config;

    config->num_carrier_blocks = num_carrier_blocks;
    config->num_entropy_blocks = num_entropy_blocks;
    config->map_entry_sz = CARRIER_HASH_SZ + ENTROPY_HASH_SZ + (sizeof(struct afs_map_tuple) * config->num_carrier_blocks);
    config->unused_space_per_block = (AFS_BLOCK_SIZE - SHA512_SZ) % config->map_entry_sz;
    config->num_map_entries_per_block = (AFS_BLOCK_SIZE - SHA512_SZ) / config->map_entry_sz;
    config->num_blocks = config->instance_size / AFS_BLOCK_SIZE;

    // Kernel doesn't support floating point math. We need to round up.
    config->num_map_blocks = config->num_blocks / config->num_map_entries_per_block;
    config->num_map_blocks += (config->num_blocks % config->num_map_entries_per_block) ? 1 : 0;

    // We store a certain amount of pointers to map blocks in the SB itself.
    // So we need to adjust for that when calculating num_ptr_blocks. We also
    // exploit unsigned math.
    if ((config->num_map_blocks - NUM_MAP_BLKS_IN_SB) > config->num_map_blocks) {
        // We can store everything in the SB itself.
        config->num_ptr_blocks = 0;
    } else {
        config->num_ptr_blocks = (config->num_map_blocks - NUM_MAP_BLKS_IN_SB) / NUM_MAP_BLKS_IN_PB;
        config->num_ptr_blocks += ((config->num_map_blocks - NUM_MAP_BLKS_IN_SB) % NUM_MAP_BLKS_IN_PB) ? 1 : 0;
    }
   
    afs_debug("Number carrier blocks per tuple: %u", config->num_carrier_blocks);
    afs_debug("Number entropy blocks per tuple: %u", config->num_entropy_blocks); 
    afs_debug("Map entry size: %u", config->map_entry_sz);
    afs_debug("Unused: %u | Entries per block: %u", config->unused_space_per_block, config->num_map_entries_per_block);
    afs_debug("Blocks: %u", config->num_blocks);
    afs_debug("Map blocks: %u", config->num_map_blocks);
    afs_debug("Ptr blocks: %u", config->num_ptr_blocks);
}

/**
 * Create the Artifice map and initialize it to
 * invalids.
 */
int
afs_create_map(struct afs_private *context)
{
    struct afs_config *config = &context->config;
    struct afs_map_tuple *map_tuple = NULL;
    uint8_t *map_entries = NULL;
    uint8_t map_entry_sz;
    uint32_t num_blocks;
    uint32_t num_carrier_blocks;
    uint32_t i, j;
    int ret = 0;

    map_entry_sz = config->map_entry_sz;
    num_blocks = config->num_blocks;
    num_carrier_blocks = config->num_carrier_blocks;

    // Allocate all required map_entries.
    map_entries = vmalloc(num_blocks * map_entry_sz);
    afs_action(map_entries, ret = -ENOMEM, done, "could not allocate map entries [%d]", ret);
    afs_debug("allocated Artifice map");

    for (i = 0; i < num_blocks; i++) {
        map_tuple = (struct afs_map_tuple *)(map_entries + (i * map_entry_sz));
        for (j = 0; j < num_carrier_blocks; j++) {
            map_tuple->carrier_block_ptr = AFS_INVALID_BLOCK;
            //map_tuple->entropy_block_ptr = AFS_INVALID_BLOCK;
            map_tuple->checksum = 0;
            map_tuple += 1;
        }
        // map_tuple now points to the beginning of the hash and the entropy.
        memset(map_tuple, 0, CARRIER_HASH_SZ + ENTROPY_HASH_SZ);
    }
    afs_debug("initialized Artifice map");
    context->afs_map = map_entries;
    ret = 0;

done:
    return ret;
}

/**
 * Fill an Artifice map with values from the
 * metadata.
 */
int
afs_fill_map(struct afs_super_block *sb, struct afs_private *context)
{
    struct afs_config *config = &context->config;
    struct afs_ptr_block *ptr_block = NULL;
    uint8_t *afs_map = NULL;
    uint8_t *map_block = NULL;
    uint8_t *map_block_hash = NULL;
    uint8_t *map_block_unused = NULL;
    uint8_t *map_block_entries = NULL;
    uint8_t map_entry_sz;
    uint8_t num_map_entries_per_block;
    uint32_t next_block;
    uint32_t entries_read;
    uint32_t entries_left;
    uint32_t i, j;
    int ret = 0;
    uint32_t data_sector_offset = context->passive_fs.data_start_off;

    // Acquire the map.
    afs_map = context->afs_map;

    map_entry_sz = config->map_entry_sz;
    num_map_entries_per_block = config->num_map_entries_per_block;

    map_block = kmalloc(AFS_BLOCK_SIZE, GFP_KERNEL);
    ptr_block = kmalloc(sizeof(*ptr_block), GFP_KERNEL);
    afs_action(map_block && ptr_block, ret = -ENOMEM, err, "could not allocate memory for map data [%d]", ret);

    // Read in everything from the super block first.
    entries_read = 0;
    for (i = 0; i < NUM_MAP_BLKS_IN_SB; i++) {
        // Read map block.
        ret = read_page(map_block, context->bdev, sb->map_block_ptrs[i], data_sector_offset, false);
        afs_assert(!ret, read_err, "could not read map block [%d:%u]", ret, entries_read % config->num_map_blocks);
        allocation_set(&context->vector, sb->map_block_ptrs[i]);

        // Offset into the block.
        map_block_hash = map_block;
        map_block_unused = map_block_hash + SHA512_SZ;
        map_block_entries = map_block_unused + config->unused_space_per_block;
        // TODO: Calculate and verify hash.

        entries_left = config->num_blocks - entries_read;
        if (entries_left <= num_map_entries_per_block) {
            memcpy(afs_map + (entries_read * map_entry_sz), map_block_entries, entries_left * map_entry_sz);
            entries_read += entries_left;
            break;
        } else {
            memcpy(afs_map + (entries_read * map_entry_sz), map_block_entries, num_map_entries_per_block * map_entry_sz);
            entries_read += num_map_entries_per_block;
        }
    }
    afs_debug("super block's map blocks read");

    // Begin reading from pointer blocks.
    for (i = 0; i < config->num_ptr_blocks; i++) {
        next_block = (i == 0) ? sb->first_ptr_block : ptr_block->next_ptr_block;
        ret = read_page(ptr_block, context->bdev, next_block, data_sector_offset, false);
        afs_assert(!ret, read_err, "could not read pointer block [%d:%u]", ret, i);
        allocation_set(&context->vector, next_block);
        // TODO: Calculate and verify hash of ptr_block.

        for (j = 0; j < NUM_MAP_BLKS_IN_PB; i++) {
            // Read map block.
            ret = read_page(map_block, context->bdev, ptr_block->map_block_ptrs[j], data_sector_offset, false);
            afs_assert(!ret, read_err, "could not read map block [%d:%u]", ret, entries_read % config->num_map_blocks);
            allocation_set(&context->vector, ptr_block->map_block_ptrs[j]);

            // Offset into the block.
            map_block_hash = map_block;
            map_block_unused = map_block_hash + SHA512_SZ;
            map_block_entries = map_block_unused + config->unused_space_per_block;
            // TODO: Calculate and verify hash.

            entries_left = config->num_blocks - entries_read;
            if (entries_left <= num_map_entries_per_block) {
                memcpy(afs_map + (entries_read * map_entry_sz), map_block_entries, entries_left * map_entry_sz);
                entries_read += entries_left;
                break;
            } else {
                memcpy(afs_map + (entries_read * map_entry_sz), map_block_entries, num_map_entries_per_block * map_entry_sz);
                entries_read += num_map_entries_per_block;
            }
        }

        // If we break into here because no more entries were left,
        // then clearly we shouldn't even have any more pointer blocks
        // left.
    }
    afs_action(entries_read == config->num_blocks, ret = -EIO, read_err,
        "read incorrect amount [%u:%u]", entries_read, config->num_blocks);
    afs_debug("pointer blocks' map blocks read");

    kfree(ptr_block);
    kfree(map_block);
    return 0;

read_err:
    if (ptr_block)
        kfree(ptr_block);
    if (map_block)
        kfree(map_block);

err:
    return ret;
}

/**
 * Create the Artifice map blocks.
 */
int
afs_create_map_blocks(struct afs_private *context)
{
    struct afs_config *config = &context->config;
    uint8_t *ptr = NULL;
    uint8_t *afs_map = NULL;
    uint8_t *map_blocks = NULL;
    uint8_t *hash = NULL;
    uint8_t *unused_space = NULL;
    uint8_t *entries_start = NULL;
    uint8_t map_entry_sz;
    uint8_t num_map_entries_per_block;
    uint32_t num_map_blocks;
    uint32_t num_blocks;
    uint32_t entries_written;
    uint32_t entries_left;
    uint32_t i;
    int ret = 0;

    afs_map = context->afs_map;
    map_entry_sz = config->map_entry_sz;
    num_map_entries_per_block = config->num_map_entries_per_block;
    num_map_blocks = config->num_map_blocks;
    num_blocks = config->num_blocks;

    // Allocate all required map_blocks.
    map_blocks = vmalloc(AFS_BLOCK_SIZE * num_map_blocks);
    afs_action(map_blocks, ret = -ENOMEM, block_err, "could not allocate map blocks [%d]", ret);
    afs_debug("allocated Artifice map blocks");

    memset(map_blocks, 0, AFS_BLOCK_SIZE * num_map_blocks);
    entries_written = 0;

    ptr = map_blocks;
    for (i = 0; i < num_map_blocks; i++) {
        hash = ptr;
        unused_space = hash + SHA512_SZ;
        entries_start = unused_space + config->unused_space_per_block;

        entries_left = num_blocks - entries_written;
        if (entries_left <= num_map_entries_per_block) {
            memcpy(entries_start, afs_map + (entries_written * map_entry_sz), entries_left * map_entry_sz);
            entries_written += entries_left;
        } else {
            memcpy(entries_start, afs_map + (entries_written * map_entry_sz), num_map_entries_per_block * map_entry_sz);
            entries_written += num_map_entries_per_block;
        }

        hash_sha512(entries_start, AFS_BLOCK_SIZE - SHA512_SZ - config->unused_space_per_block, hash);
        ptr += AFS_BLOCK_SIZE;
    }
    afs_action(entries_written == num_blocks, ret = -EIO, write_err,
        "wrote incorrect amount [%u:%u]", entries_written, num_blocks);
    afs_debug("initialized Artifice map blocks");

    context->afs_map_blocks = map_blocks;
    return 0;

write_err:
    vfree(map_blocks);

block_err:
    return ret;
}

/**
 * Write map blocks to pointer blocks.
 */
int
write_map_blocks(struct afs_private *context, bool update)
{
    struct afs_config *config = &context->config;
    struct afs_super_block *sb = NULL;
    struct afs_passive_fs *fs = NULL;
    struct afs_ptr_block *afs_ptr_blocks = NULL;
    uint8_t *afs_map_blocks = NULL;
    uint32_t num_map_blocks;
    uint32_t num_ptr_blocks;
    uint32_t blocks_written;
    uint32_t blocks_left;
    uint32_t block_num;
    int64_t i, j;
    uint32_t data_sector_offset = context->passive_fs.data_start_off;
    int ret = 0;

    sb = &context->super_block;
    fs = &context->passive_fs;
    afs_map_blocks = context->afs_map_blocks;

    num_map_blocks = config->num_map_blocks;
    num_ptr_blocks = config->num_ptr_blocks;

    // First utilize all the map block pointers from the super block.
    blocks_written = 0;
    for (i = 0; i < NUM_MAP_BLKS_IN_SB; i++) {
        blocks_left = num_map_blocks - blocks_written;
        if (!blocks_left) {
            afs_debug("super block map block pointers filled");
            return 0;
        }

        if (!update) {
            block_num = acquire_block(fs, &context->vector);
            afs_action(block_num != AFS_INVALID_BLOCK, ret = -ENOSPC, done, "no more free blocks");
            sb->map_block_ptrs[i] = block_num;
        } else {
            block_num = sb->map_block_ptrs[i];
        }
        ret = write_page(afs_map_blocks + (blocks_written * AFS_BLOCK_SIZE), context->bdev, block_num, data_sector_offset, true);
        afs_assert(!ret, done, "could not write blocks [%d:%u]", ret, blocks_written);
        blocks_written += 1;
    }
    afs_debug("super block map block pointers filled");

    // If we even reach this point, then it means that some ptr_blocks are required.
    afs_ptr_blocks = context->afs_ptr_blocks;
    for (i = 0; i < num_ptr_blocks; i++) {
        for (j = 0; j < NUM_MAP_BLKS_IN_PB; j++) {
            blocks_left = num_map_blocks - blocks_written;
            if (!blocks_left) {
                break;
            }

            if (!update) {
                block_num = acquire_block(fs, &context->vector);
                afs_action(block_num != AFS_INVALID_BLOCK, ret = -ENOSPC, done, "no more free blocks");
                afs_ptr_blocks[i].map_block_ptrs[j] = block_num;
            } else {
                block_num = afs_ptr_blocks[i].map_block_ptrs[j];
            }
            ret = write_page(afs_map_blocks + (blocks_written * AFS_BLOCK_SIZE), context->bdev, block_num, data_sector_offset, true);
            afs_assert(!ret, done, "could not write block [%d:%u]", ret, blocks_written);
            blocks_written += 1;
        }
    }
    afs_debug("pointer blocks map block pointers filled");
    ret = 0;

done:
    return ret;
}

/**
 * Write out the ptr blocks to disk.
 */
int
write_ptr_blocks(struct afs_super_block *sb, struct afs_passive_fs *fs, struct afs_private *context)
{
    struct afs_config *config = &context->config;
    struct afs_ptr_block *ptr_blocks = NULL;
    uint8_t ptr_block_digest[SHA1_SZ];
    uint32_t num_ptr_blocks;
    uint32_t block_num;
    int64_t i;
    uint32_t data_sector_offset = context->passive_fs.data_start_off;
    int ret = 0;

    // Write all the map blocks.
    ret = write_map_blocks(context, false);
    afs_debug("map blocks written");
    afs_assert(!ret, done, "could not write Artifice map blocks [%d]", ret);

    num_ptr_blocks = config->num_ptr_blocks;
    if (!num_ptr_blocks) {
        return 0;
    }

    // Write out the ptr blocks themselves to disk. Needs to be done in reverse
    // as ptr blocks themselves hold pointers to other ptr blocks.
    ptr_blocks = context->afs_ptr_blocks;
    for (i = num_ptr_blocks - 1; i >= 0; i--) {
        // The last ptr block needs to have an invalid pointer
        // to the next.
        if (i == num_ptr_blocks - 1) {
            ptr_blocks[i].next_ptr_block = AFS_INVALID_BLOCK;
        }

        // Calculate hash of the ptr blocks.
        hash_sha1((uint8_t *)(ptr_blocks + i) + SHA128_SZ, sizeof(*ptr_blocks) - SHA128_SZ, ptr_block_digest);
        memcpy(ptr_blocks[i].hash, ptr_block_digest, sizeof(ptr_blocks[i].hash));

        // Write to disk and save pointer.
        block_num = acquire_block(fs, &context->vector);
        afs_action(block_num != AFS_INVALID_BLOCK, ret = -ENOSPC, done, "no more free blocks");
        ret = write_page(ptr_blocks + i, context->bdev, block_num, data_sector_offset, false);
        afs_assert(!ret, done, "could not write ptr block [%d:%llu]", ret, i);

        if (i == 0) {
            sb->first_ptr_block = block_num;
        } else {
            ptr_blocks[i - 1].next_ptr_block = block_num;
        }
    }
    afs_debug("pointer blocks written");
    ret = 0;

done:
    return ret;
}

int chain_hash_superblock(uint32_t *pass_hash[SHA1_SZ], uint32_t *sb_block, uint32_t block_device_size, struct afs_passive_fs *fs){
    while(binary_search(fs->block_list, *sb_block, fs->list_len) == -1){
        
    }
    return 0;
}

/**
 * Write the super block onto the disk.
 */
int
write_super_block(struct afs_super_block *sb, struct afs_passive_fs *fs, struct afs_private *context)
{
    uint32_t sb_block[NUM_SUPERBLOCK_REPLICAS];
    struct afs_config *config = &context->config;
    struct afs_ptr_block *ptr_blocks = NULL;
    int ret = 0;
    int i = 0;
    uint8_t pass_hash[NUM_SUPERBLOCK_REPLICAS][SHA1_SZ];
    uint32_t block_device_size = config->bdev_size / AFS_SECTORS_PER_BLOCK;

    //hash passphrase and determine location of first superblock
    hash_sha1(context->args.passphrase, PASSPHRASE_SZ, pass_hash[0]);
    memcpy(&sb_block[0], pass_hash[0], sizeof(uint32_t));
    sb_block[0] = sb_block[0] % block_device_size;

    // Reserve space for the super block location.
    // if the hashed block is available, rehash and try again if it is not
    while(binary_search(fs->block_list, sb_block[0], fs->list_len) == -1){
        hash_sha1(pass_hash[0], SHA1_SZ, pass_hash[0]);
        memcpy(&sb_block[0] , pass_hash[0], sizeof(uint32_t));
        sb_block[0] = sb_block[0] % block_device_size; 
    }
    allocation_set(&context->vector, sb_block[0]);

    //generate list of additional superblock locations
    //Iteratively hash like before
    for(i = 1; i < NUM_SUPERBLOCK_REPLICAS; i++){
        hash_sha1(pass_hash[i-1], SHA1_SZ, pass_hash[i]);
        memcpy(&sb_block[i], pass_hash[i], sizeof(uint32_t));
        sb_block[i] = sb_block[i] % block_device_size;
        while(binary_search(fs->block_list, sb_block[i], fs->list_len) == -1){
            hash_sha1(pass_hash[i], SHA1_SZ, pass_hash[i]);   
            memcpy(&sb_block[i] , pass_hash[i], sizeof(uint32_t));
            sb_block[i] = sb_block[i] % block_device_size;
        }
        allocation_set(&context->vector, sb_block[i]);
    }

    // Build the Artifice Map.
    ret = afs_create_map(context);
    afs_assert(!ret, map_err, "could not create Artifice map [%d]", ret);

    // Build the Artifice Map Blocks.
    ret = afs_create_map_blocks(context);
    afs_assert(!ret, map_block_err, "could not create Artifice map blocks [%d]", ret);

    // Allocate the Artifice Pointer Blocks.
    ptr_blocks = kmalloc(config->num_ptr_blocks * sizeof(*ptr_blocks), GFP_KERNEL);
    afs_action(ptr_blocks, ret = -ENOMEM, ptr_block_err, "could not allocate ptr_blocks [%d]", ret);
    memset(ptr_blocks, 0, config->num_ptr_blocks * sizeof(*ptr_blocks));
    context->afs_ptr_blocks = ptr_blocks;

    // Build the Artifice Pointer Blocks.
    sb->first_ptr_block = AFS_INVALID_BLOCK;
    afs_debug("writing pointer blocks");
    ret = write_ptr_blocks(sb, fs, context);
    afs_debug("pointer blocks written");
    afs_assert(!ret, ptr_block_err, "could not write pointer blocks [%d]", ret);

    // 1. Take note of the instance size.
    // 2. Take note of the entropy directory for the instance.
    // 3. Hash the super block.
    // 4. Write each block to disk.
    sb->instance_size = config->instance_size;
    strncpy(sb->entropy_dir, context->args.entropy_dir, ENTROPY_DIR_SZ);
    hash_sha256((uint8_t *)sb + SHA256_SZ, sizeof(*sb) - SHA256_SZ, sb->sb_hash);
    for(i = 0; i < NUM_SUPERBLOCK_REPLICAS; i++){
        ret = write_page(sb, context->bdev, fs->block_list[sb_block[i]], context->passive_fs.data_start_off, false);
        afs_assert(!ret, sb_err, "could not write super block [%d]", ret);
        afs_debug("super blocks written to disk [block: %u]", sb_block[i]);
    }

    // We don't need the map blocks anymore.
    vfree(context->afs_map_blocks);
    return 0;

sb_err:
    kfree(context->afs_ptr_blocks);

ptr_block_err:
    vfree(context->afs_map_blocks);

map_block_err:
    vfree(context->afs_map);

map_err:
    return ret;
}

/**
 * Traverse through the Artifice map and
 * rebuild the allocation vector.
 */
static void
rebuild_allocation_vector(struct afs_private *context)
{
    struct afs_config *config = &context->config;
    struct afs_map_tuple *map_tuple = NULL;
    uint8_t *afs_map = NULL;
    uint8_t num_carrier_blocks;
    uint8_t map_entry_sz;
    uint32_t num_entries;
    uint32_t i, j;

    afs_map = context->afs_map;
    num_entries = config->num_blocks;
    num_carrier_blocks = config->num_carrier_blocks;
    map_entry_sz = config->map_entry_sz;

    for (i = 0; i < num_entries; i++) {
        map_tuple = (struct afs_map_tuple *)(afs_map + (i * map_entry_sz));
        for (j = 0; j < num_carrier_blocks; j++) {
            allocation_set(&context->vector, map_tuple->carrier_block_ptr);
            map_tuple += 1;
        }
    }
}

/**
 * Initialize the entires into the pointer blocks.
 */
static int
rebuild_ptr_blocks(struct afs_private *context)
{
    struct afs_config *config = &context->config;
    struct afs_ptr_block *afs_ptr_blocks = NULL;
    uint32_t num_ptr_blocks;
    uint32_t block_num;
    uint32_t i;
    uint32_t data_sector_offset = context->passive_fs.data_start_off;
    int ret;

    // Acquire pointer to pointer blocks.
    afs_ptr_blocks = context->afs_ptr_blocks;

    num_ptr_blocks = config->num_ptr_blocks;
    for (i = 0; i < num_ptr_blocks; i++) {
        block_num = (i == 0) ? context->super_block.first_ptr_block : afs_ptr_blocks[i].next_ptr_block;
        ret = read_page(afs_ptr_blocks + i, context->bdev, block_num, data_sector_offset, false);
        afs_assert(!ret, done, "could not read pointer block [%d:%u]", ret, block_num);
    }
    ret = 0;

done:
    return ret;
}

/**
 * Find the super block on the disk.
 *
 */
int
find_super_block(struct afs_super_block *sb, struct afs_private *context)
{
    uint32_t sb_block[NUM_SUPERBLOCK_REPLICAS];
    struct afs_config *config = &context->config;
    struct afs_ptr_block *ptr_blocks = NULL;
    uint8_t sb_digest[SHA256_SZ];
    int ret = 0;
    uint32_t sb_tries = 0;
    uint8_t pass_hash[NUM_SUPERBLOCK_REPLICAS][SHA1_SZ];
    uint32_t block_device_size = config->bdev_size / AFS_SECTORS_PER_BLOCK;

    hash_sha1(context->args.passphrase, PASSPHRASE_SZ, pass_hash[0]);
    memcpy(&sb_block[0], pass_hash[0], sizeof(uint32_t));
    sb_block[0] = sb_block[0] % block_device_size;
    
    //TODO make it so we can somehow keep track of how many of these we lose
    while (sb_tries < 32){
        //if our superblock is not present in the block list then we can assume it doesn't exist
        while(binary_search(context->passive_fs.block_list, sb_block[0], context->passive_fs.list_len) == -1){
            hash_sha1(pass_hash[0], SHA1_SZ, pass_hash[0]);
            memcpy(&sb_block[0], pass_hash[0], sizeof(uint32_t));
            sb_block[0] = sb_block[0] % block_device_size;
            sb_tries++;
        }
        //attempt to read superblock
        ret = read_page(sb, context->bdev, context->passive_fs.block_list[sb_block[0]], context->passive_fs.data_start_off, false);
        afs_assert(!ret, err, "could not read super block page [%d]", ret);

        // Check for corruption.
        hash_sha256((uint8_t *)sb + SHA256_SZ, sizeof(*sb) - SHA256_SZ, sb_digest);
        ret = memcmp(sb->sb_hash, sb_digest, SHA256_SZ);
        if(ret){
            afs_debug("Superblock read attempt %u failed", sb_tries);
            sb_tries++;
        }else{
            break; 
        }
    }
    allocation_set(&context->vector, sb_block[0]);

    afs_action(!ret, ret = -ENOENT, err, "super block corrupted");

    // Confirm size is same.
    afs_action(config->instance_size == sb->instance_size, ret = -EINVAL, err,
        "incorrect size provided [%llu:%llu]", config->instance_size, sb->instance_size);

    // TODO: Acquire from RS params in SB.
    build_configuration(context, 4, 1);

    ret = afs_create_map(context);
    afs_assert(!ret, err, "could not create artifice map [%d]", ret);

    ret = afs_fill_map(sb, context);
    afs_assert(!ret, map_fill_err, "could not fill artifice map [%d]", ret);
    rebuild_allocation_vector(context);
    afs_debug("Artifice map rebuilt");

    // Create the Artifice Pointer Blocks. These are required for when
    // we need to re-write the map blocks.
    ptr_blocks = kmalloc(config->num_ptr_blocks * sizeof(*ptr_blocks), GFP_KERNEL);
    afs_action(ptr_blocks, ret = -ENOMEM, map_fill_err, "could not allocate ptr_blocks [%d]", ret);
    context->afs_ptr_blocks = ptr_blocks;

    ret = rebuild_ptr_blocks(context);
    afs_assert(!ret, ptr_block_err, "could not rebuild Artifice pointer blocks [%d]", ret);
    afs_debug("Artifice pointer blocks rebuilt");

    return 0;

ptr_block_err:
    kfree(context->afs_map_blocks);

map_fill_err:
    vfree(context->afs_map);

err:
    return ret;
}
