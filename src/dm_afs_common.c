/**
 * Author: Yash Gupta <ygupta@ucsc.edu>, Austen Barker <atbarker@ucsc.edu>
 * Copyright: UC Santa Cruz, SSRC
 */
#include <dm_afs.h>
#include <dm_afs_modules.h>
#include <linux/errno.h>
#include <linux/crypto.h>
#include <linux/random.h>
#include <linux/rslib.h>
#include <crypto/hash.h>

/**
 * Set the usage of a block in the allocation vector.
 */
bool 
allocation_set(struct afs_private *context, uint32_t index)
{
    spin_lock(&context->allocation_lock);
    if (bit_vector_get(context->allocation_vec, index)) {
        spin_unlock(&context->allocation_lock);
        return false;
    }
    bit_vector_set(context->allocation_vec, index);
    spin_unlock(&context->allocation_lock);

    return true;
}

/**
 * Clear the usage of a block in the allocation vector.
 */
void 
allocation_free(struct afs_private *context, uint32_t index)
{
    spin_lock(&context->allocation_lock);
    bit_vector_clear(context->allocation_vec, index);
    spin_unlock(&context->allocation_lock);
}

/**
 * Get the state of a block in the allocation vector.
 */
uint8_t 
allocation_get(struct afs_private *context, uint32_t index)
{
    uint8_t bit;

    spin_lock(&context->allocation_lock);
    bit = bit_vector_get(context->allocation_vec, index);
    spin_unlock(&context->allocation_lock);

    return bit;
}

/**
 * Callback function to signify the completion 
 * of an IO transfer to a block device.
 */
static void
__callback_blkdev_io(struct bio *bio)
{
    struct completion *event = bio->bi_private;

    //afs_debug("completed event: %p\n", event);
    complete(event);
}

/**
 * Read or write to an block device.
 * 
 * @request  I/O request structure
 * @return  0       Successfully performed the I/O.
 * @return  <0      Error.
 */
int
afs_blkdev_io(struct afs_io *request)
{
    const int page_offset = 0;
    int ret;
    struct bio *bio = NULL;
    struct completion event;

    // Any IO to a block device in the kernel is done using bio.
    // We need to allocate a bio with a single IO vector and set
    // its corresponding fields.
    // 
    // bio's are asynchronous and hence an event based waiting
    // loop is used to provide the illusion of synchronous IO.
    init_completion(&event);

    bio = bio_alloc(GFP_NOIO, 1);
    afs_assert_action(!IS_ERR(bio), ret = PTR_ERR(bio), alloc_err, "could not allocate bio [%d]", ret);

    switch (request->type) {
        case IO_READ:
            bio->bi_opf |= REQ_OP_READ;
            break;

        case IO_WRITE:
            bio->bi_opf |= REQ_OP_WRITE;
            break;

        default:
            afs_assert_action(0, ret = -EINVAL, invalid_type, "invalid IO type [%d]", request->type);
    }
    bio->bi_opf |= REQ_SYNC;
    // TODO: Perhaps make ASYNC for performance considerations?

    bio_set_dev(bio, request->bdev);
    bio->bi_iter.bi_sector = request->io_sector;
    bio->bi_private = &event;
    bio->bi_end_io = __callback_blkdev_io;
    bio_add_page(bio, request->io_page, request->io_size, page_offset);
    
    submit_bio(bio);
    wait_for_completion(&event);

    return 0;

invalid_type:
    bio_endio(bio);

alloc_err:
    return ret;
}

/**
 * Read a single page.
 */
int 
read_page(void *page, struct block_device *bdev, uint32_t block_num, bool used_vmalloc)
{
    struct afs_io request;
    struct page *page_structure;
    uint64_t sector_num;
    int ret;

    // Make sure page is aligned.
    afs_assert_action(!((uint64_t)page & (AFS_BLOCK_SIZE-1)), ret = -EINVAL, done, "page is not aligned [%d]", ret);

    // Acquire page structure and sector offset.
    page_structure = (used_vmalloc)? vmalloc_to_page(page): virt_to_page(page);
    sector_num = (block_num * AFS_BLOCK_SIZE) / AFS_SECTOR_SIZE;

    // Build the request.
    request.bdev = bdev;
    request.io_page = page_structure;
    request.io_sector = sector_num;
    request.io_size = AFS_BLOCK_SIZE;
    request.type = IO_READ;

    ret = afs_blkdev_io(&request);
    afs_assert(!ret, done, "error in reading block device [%d]", ret);

done:
    return ret;
}

/**
 * Write a single page.
 */
int 
write_page(const void *page, struct block_device *bdev, uint32_t block_num, bool used_vmalloc)
{
    struct afs_io request;
    struct page *page_structure;
    uint64_t sector_num;
    int ret;

    // Make sure page is aligned.
    afs_assert_action(!((uint64_t)page & (AFS_BLOCK_SIZE-1)), ret = -EINVAL, done, "page is not aligned [%d]", ret);

    // Acquire page structure and sector offset.
    page_structure = (used_vmalloc)? vmalloc_to_page(page): virt_to_page(page);
    sector_num = (block_num * AFS_BLOCK_SIZE) / AFS_SECTOR_SIZE;

    // Build the request.
    request.bdev = bdev;
    request.io_page = page_structure;
    request.io_sector = sector_num;
    request.io_size = AFS_BLOCK_SIZE;
    request.type = IO_WRITE;

    ret = afs_blkdev_io(&request);
    afs_assert(!ret, done, "error in writing block device [%d]", ret);

done:
    return ret;
}

/**
 * Build the configuration for an instance.
 */
void 
build_configuration(struct afs_private *context, uint8_t num_carrier_blocks)
{
    context->num_carrier_blocks = num_carrier_blocks;
    context->map_entry_sz = SHA128_SZ + ENTROPY_HASH_SZ + (sizeof(struct afs_map_tuple) * context->num_carrier_blocks);
    context->unused_space_per_table = (AFS_BLOCK_SIZE - SHA512_SZ) % context->map_entry_sz;
    context->num_map_entries_per_table = (AFS_BLOCK_SIZE - SHA512_SZ) / context->map_entry_sz;
    context->num_blocks = context->instance_size / AFS_BLOCK_SIZE;

    // Kernel doesn't support floating point math. We need to round up.
    context->num_map_tables = context->num_blocks / context->num_map_entries_per_table;
    context->num_map_tables += (context->num_blocks % context->num_map_entries_per_table)? 1: 0;

    // We store a certain amount of pointers to map tables in the SB itself. 
    // So we need to adjust for that when calculating num_map_blocks. We also
    // exploit unsigned math.
    if ((context->num_map_tables - SB_MAP_PTRS_SZ) > context->num_map_tables) {
        // We can store everything in the SB itself.
        context->num_map_blocks = 0;
    } else {
        context->num_map_blocks = (context->num_map_tables - SB_MAP_PTRS_SZ) / MAP_BLK_PTRS_SZ;
        context->num_map_blocks += ((context->num_map_tables - SB_MAP_PTRS_SZ) % MAP_BLK_PTRS_SZ)? 1: 0;
    }

    afs_debug("Map entry size: %u", context->map_entry_sz);
    afs_debug("Unused: %u | Entries per table: %u", context->unused_space_per_table, context->num_map_entries_per_table);
    afs_debug("Blocks: %u", context->num_blocks);
    afs_debug("Map tables: %u", context->num_map_tables);
    afs_debug("Map blocks: %u", context->num_map_blocks);
}

/**
 * Create the Artifice map and initialize it to
 * invalids.
 */
static int
afs_create_map(struct afs_private *context)
{
    struct afs_map_tuple *map_tuple = NULL;
    uint8_t *map_entries = NULL;
    uint8_t  map_entry_sz;
    uint32_t num_blocks;
    uint32_t i, j;
    int ret = 0;

    map_entry_sz = context->map_entry_sz;
    num_blocks = context->num_blocks;

    // Allocate all required map_entries.
    map_entries = vmalloc(num_blocks * map_entry_sz);
    afs_assert_action(map_entries, ret = -ENOMEM, done, "could not allocate map entries [%d]", ret);
    afs_debug("allocated Artifice map");

    for (i = 0; i < num_blocks; i++) {
        map_tuple = (struct afs_map_tuple*)(map_entries + (i * map_entry_sz));
        for (j = 0; j < context->num_carrier_blocks; j++) {
            map_tuple->carrier_block_pointer = AFS_INVALID_BLOCK;
            map_tuple->entropy_block_pointer = AFS_INVALID_BLOCK;
            map_tuple->checksum = 0;
            map_tuple += 1;
        }
        // map_tuple now points to the beginning of the hash and the entropy.
        memset(map_tuple, 0, SHA128_SZ + ENTROPY_HASH_SZ);
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
static int
afs_fill_map(struct afs_super_block *sb, struct afs_private *context)
{
    struct afs_map_block *map_block = NULL;
    uint8_t *afs_map = NULL;
    uint8_t *map_table = NULL;
    uint8_t *map_table_hash = NULL;
    uint8_t *map_table_unused = NULL;
    uint8_t *map_table_entries = NULL;
    uint8_t  map_entry_sz;
    uint8_t  num_map_entries_per_table;
    uint32_t next_block;
    uint32_t entries_read;
    uint32_t entries_left;
    uint32_t i, j;
    int ret = 0;

    // Acquire the map.
    afs_map = context->afs_map;

    map_entry_sz = context->map_entry_sz;
    num_map_entries_per_table = context->num_map_entries_per_table;

    map_table = kmalloc(AFS_BLOCK_SIZE, GFP_KERNEL);
    map_block = kmalloc(sizeof(*map_block), GFP_KERNEL);
    afs_assert_action(map_block && map_table, ret = -ENOMEM, err, "could not allocate memory for map data [%d]", ret);

    // Read in everything from the super block first.
    entries_read = 0;
    for (i = 0; i < SB_MAP_PTRS_SZ; i++) {
        // Read map table.
        ret = read_page(map_table, context->bdev, sb->map_table_pointers[i], false);
        afs_assert(!ret, read_err, "could not read map table [%d:%u]", ret, entries_read % context->num_map_tables);
        allocation_set(context, sb->map_table_pointers[i]);

        // Offset into the table.
        map_table_hash = map_table;
        map_table_unused = map_table_hash + SHA512_SZ;
        map_table_entries = map_table_unused + context->unused_space_per_table;
        // TODO: Calculate and verify hash.

        entries_left = context->num_blocks - entries_read;
        if (entries_left <= num_map_entries_per_table) {
            memcpy(afs_map + (entries_read * map_entry_sz), map_table_entries, entries_left * map_entry_sz);
            entries_read += entries_left;
            break;
        } else {
            memcpy(afs_map + (entries_read * map_entry_sz), map_table_entries, num_map_entries_per_table * map_entry_sz);
            entries_read += num_map_entries_per_table;
        }
    }
    afs_debug("super block's map tables read");

    // Begin reading from map blocks.
    for (i = 0; i < context->num_map_blocks; i++) {
        next_block = (i == 0)? sb->next_map_block: map_block->next_map_block;
        ret = read_page(map_block, context->bdev, next_block, false);
        afs_assert(!ret, read_err, "could not read map block [%d:%u]", ret, i);
        allocation_set(context, next_block);
        // TODO: Calculate and verify hash of map_block.

        for (j = 0; j < MAP_BLK_PTRS_SZ; i++) {
            // Read map table.
            ret = read_page(map_table, context->bdev, map_block->map_table_pointers[j], false);
            afs_assert(!ret, read_err, "could not read map table [%d:%u]", ret, entries_read % context->num_map_tables);
            allocation_set(context, map_block->map_table_pointers[j]);

            // Offset into the table.
            map_table_hash = map_table;
            map_table_unused = map_table_hash + SHA512_SZ;
            map_table_entries = map_table_unused + context->unused_space_per_table;
            // TODO: Calculate and verify hash.

            entries_left = context->num_blocks - entries_read;
            if (entries_left <= num_map_entries_per_table) {
                memcpy(afs_map + (entries_read * map_entry_sz), map_table_entries, entries_left * map_entry_sz);
                entries_read += entries_left;
                break;
            } else {
                memcpy(afs_map + (entries_read * map_entry_sz), map_table_entries, num_map_entries_per_table * map_entry_sz);
                entries_read += num_map_entries_per_table;
            }
        }

        // If we break into here because no more entries were left,
        // then clearly we shouldn't even have any more map blocks 
        // left.
    }
    afs_assert_action(entries_read == context->num_blocks, ret = -EIO, read_err, 
                      "read incorrect amount [%u:%u]", entries_read, context->num_blocks);
    afs_debug("map blocks' map tables read");

    kfree(map_block);
    kfree(map_table);
    return 0;

read_err:
    if (map_block) kfree(map_block);
    if (map_table) kfree(map_table);

err:
    return ret;
}

/**
 * Create the Artifice map tables.
 */
static int
afs_create_map_tables(struct afs_private *context)
{
    uint8_t *ptr = NULL;
    uint8_t *afs_map = NULL;
    uint8_t *map_tables = NULL;
    uint8_t *hash = NULL;
    uint8_t *unused_space = NULL;
    uint8_t *entries_start = NULL;
    uint8_t  map_entry_sz;
    uint8_t  num_map_entries_per_table;
    uint32_t num_map_tables;
    uint32_t num_blocks;
    uint32_t entries_written;
    uint32_t entries_left;
    uint32_t i;
    int ret = 0;

    afs_map = context->afs_map;
    map_entry_sz = context->map_entry_sz;
    num_map_entries_per_table = context->num_map_entries_per_table;
    num_map_tables = context->num_map_tables;
    num_blocks = context->num_blocks;

    // Allocate all required map_tables.
    map_tables = vmalloc(AFS_BLOCK_SIZE * num_map_tables);
    afs_assert_action(map_tables, ret = -ENOMEM, table_err, "could not allocate map tables [%d]", ret);
    afs_debug("allocated Artifice map tables");

    memset(map_tables, 0, AFS_BLOCK_SIZE * num_map_tables);
    entries_written = 0;

    ptr = map_tables;
    for (i = 0; i < num_map_tables; i++) {
        hash = ptr;
        unused_space = hash + SHA512_SZ;
        entries_start = unused_space + context->unused_space_per_table;
        
        entries_left = num_blocks - entries_written;
        if (entries_left <= num_map_entries_per_table) {
            memcpy(entries_start, afs_map + (entries_written * map_entry_sz), entries_left * map_entry_sz);
            entries_written += entries_left;
        } else {
            memcpy(entries_start, afs_map + (entries_written * map_entry_sz), num_map_entries_per_table * map_entry_sz);
            entries_written += num_map_entries_per_table;
        }

        // Compute the hash for this map table.
        ret = hash_sha512(entries_start, AFS_BLOCK_SIZE - SHA512_SZ - context->unused_space_per_table, hash);
        afs_assert(!ret, err, "could not computer hash of map table [%d:%u]", ret, i);

        ptr += AFS_BLOCK_SIZE;
    }
    afs_assert_action(entries_written == num_blocks, ret = -EIO, err, 
                      "wrote incorrect amount [%u:%u]", entries_written, num_blocks);
    afs_debug("initialized Artifice map tables");
    
    context->afs_map_tables = map_tables;
    return 0;
    
err:
    vfree(map_tables);

table_err:
    return ret;
}

/**
 * Write map tables to map blocks.
 */
int
write_map_tables(struct afs_private *context, bool update)
{
    struct afs_super_block *sb = NULL;
    struct afs_passive_fs  *fs = NULL;
    struct afs_map_block   *afs_map_blocks = NULL;
    uint8_t *afs_map_tables = NULL;
    uint32_t num_map_tables;
    uint32_t num_map_blocks;
    uint32_t tables_written;
    uint32_t tables_left;
    uint32_t block_num;
    int64_t i, j;
    int ret = 0;

    sb = &context->super_block;
    fs = &context->passive_fs;
    afs_map_tables = context->afs_map_tables;

    num_map_tables = context->num_map_tables;
    num_map_blocks = context->num_map_blocks;

    // First utilize all the map table pointers from the super block.
    tables_written = 0;
    for (i = 0; i < SB_MAP_PTRS_SZ; i++) {
        tables_left = num_map_tables - tables_written;
        if (!tables_left) {
            afs_debug("super block map table pointers filled");
            return 0;
        }

        if (!update) {
            block_num = acquire_block(fs, context);
            afs_assert_action(block_num != AFS_INVALID_BLOCK, ret = -ENOSPC, done, "no more free blocks");
            sb->map_table_pointers[i] = block_num;
        } else {
            block_num = sb->map_table_pointers[i];
        }
        ret = write_page(afs_map_tables + (tables_written * AFS_BLOCK_SIZE), context->bdev, block_num, true);
        afs_assert(!ret, done, "could not write table [%d:%u]", ret, tables_written);
        tables_written += 1;
    }
    afs_debug("super block map table pointers filled");

    // If we even reach this point, then it means that some map_blocks are required.
    afs_map_blocks = context->afs_map_blocks;
    for (i = 0; i < num_map_blocks; i++) {
        for (j = 0; j < MAP_BLK_PTRS_SZ; j++) {
            tables_left = num_map_tables - tables_written;
            if (!tables_left) {
                break;
            }
            
            if (!update) {
                block_num = acquire_block(fs, context);
                afs_assert_action(block_num != AFS_INVALID_BLOCK, ret = -ENOSPC, done, "no more free blocks");
                afs_map_blocks[i].map_table_pointers[j] = block_num;
            } else {
                block_num = afs_map_blocks[i].map_table_pointers[j];
            }
            ret = write_page(afs_map_tables + (tables_written * AFS_BLOCK_SIZE), context->bdev, block_num, true);
            afs_assert(!ret, done, "could not write table [%d:%u]", ret, tables_written);
            tables_written += 1;
        }
        afs_map_blocks[i].next_map_block = AFS_INVALID_BLOCK;
    }
    afs_debug("map blocks map table pointers filled");
    ret = 0;

done:
    return ret;
}


/**
 * Write out the map blocks to disk.
 */
static int
write_map_blocks(struct afs_super_block *sb, struct afs_passive_fs *fs, struct afs_private *context)
{
    struct afs_map_block *map_blocks = NULL;
    uint8_t  map_block_digest[SHA1_SZ];
    uint32_t num_map_blocks;
    uint32_t block_num;
    int64_t i;
    int ret = 0;

    // Write all the map tables.
    ret = write_map_tables(context, false);
    afs_assert(!ret, done, "could not write Artifice map tables [%d]", ret);

    num_map_blocks = context->num_map_blocks;
    if (!num_map_blocks) {
        return 0;
    }

    // Write out the map blocks themselves to disk. Needs to be done in reverse
    // as map blocks themselves hold pointers to other map blocks.
    map_blocks = context->afs_map_blocks;
    for (i = num_map_blocks-1; i >= 0; i--) {
        // Calculate hash of the map blocks.
        hash_sha1((uint8_t*)(map_blocks + i) + SHA128_SZ, sizeof(*map_blocks) - SHA128_SZ, map_block_digest);
        memcpy(map_blocks[i].hash, map_block_digest, sizeof(map_blocks[i].hash));

        // Write to disk and save pointer.
        block_num = acquire_block(fs, context);
        afs_assert_action(block_num != AFS_INVALID_BLOCK, ret = -ENOSPC, done, "no more free blocks");
        ret = write_page(map_blocks + i, context->bdev, block_num, false);
        afs_assert(!ret, done, "could not write map block [%d:%llu]", ret, i);
        
        if (i == 0) {
            sb->next_map_block = block_num;
        } else {
            map_blocks[i-1].next_map_block = block_num;
        }
    }
    afs_debug("map blocks written");
    ret = 0;

done:
    return ret;
}

/**
 * Write the super block onto the disk.
 * 
 * TODO: Change sb_block location.
 */
int 
write_super_block(struct afs_super_block *sb, struct afs_passive_fs *fs, struct afs_private *context)
{
    const uint32_t sb_block = 0;
    struct afs_map_block *map_blocks = NULL;
    int ret = 0;

    // Reserve space for the super block location.
    allocation_set(context, sb_block);

    // Build the Artifice Map.
    ret = afs_create_map(context);
    afs_assert(!ret, map_err, "could not create Artifice map [%d]", ret);

    // Build the Artifice Map Table.
    ret = afs_create_map_tables(context);
    afs_assert(!ret, table_err, "could not create Artifice map table [%d]", ret);

    // Allocate the Artifice Map Blocks.
    map_blocks = kmalloc(context->num_map_blocks * sizeof(*map_blocks), GFP_KERNEL);
    afs_assert_action(map_blocks, ret = -ENOMEM, block_err, "could not allocate map_blocks [%d]", ret);
    memset(map_blocks, 0, context->num_map_blocks * sizeof(*map_blocks));
    context->afs_map_blocks = map_blocks;

    // Build the Artifice Map Blocks.
    sb->next_map_block = AFS_INVALID_BLOCK;
    ret = write_map_blocks(sb, fs, context);
    afs_assert(!ret, block_err, "could not write map blocks [%d]", ret);

    // 1. Take note of the instance size.
    // 2. Calculate the hash of the provided passphrase.
    // 3. Take note of the entropy directory for the instance.
    // 4. Hash the super block.
    // 5. Write to disk.
    sb->instance_size = context->instance_size;
    hash_sha1(context->instance_args.passphrase, PASSPHRASE_SZ, sb->hash);
    strncpy(sb->entropy_dir, context->instance_args.entropy_dir, ENTROPY_DIR_SZ);
    hash_sha256((uint8_t*)sb + SHA256_SZ, sizeof(*sb) - SHA256_SZ, sb->sb_hash);
    ret = write_page(sb, context->bdev, sb_block, false);
    afs_assert(!ret, sb_err, "could not write super block [%d]", ret);
    afs_debug("super block written to disk [block: %u]", sb_block);

    // We don't need the map blocks and the tables anymore.
    vfree(context->afs_map_tables);
    kfree(context->afs_map_blocks);
    return 0;

sb_err:
    kfree(context->afs_map_blocks);

block_err:
    vfree(context->afs_map_tables);

table_err:
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
    struct afs_map_tuple *map_tuple = NULL;
    uint8_t  *afs_map = NULL;
    uint8_t  num_carrier_blocks;
    uint8_t  map_entry_sz;
    uint32_t num_entries;
    uint32_t i, j;

    afs_map = context->afs_map;
    num_entries = context->num_blocks;
    num_carrier_blocks = context->num_carrier_blocks;
    map_entry_sz = context->map_entry_sz;

    for (i = 0; i < num_entries; i++) {
        map_tuple = (struct afs_map_tuple*)(afs_map + (i * map_entry_sz));
        for (j = 0; j < num_carrier_blocks; j++) {
            allocation_set(context, map_tuple->carrier_block_pointer);
            map_tuple += 1;
        }
    }
}

/**
 * Find the super block on the disk.
 *
 * TODO: Change sb_block location.
 */
int 
find_super_block(struct afs_super_block *sb, struct afs_private *context)
{
    const uint32_t sb_block = 0;
    uint8_t sb_digest[SHA256_SZ];
    int ret = 0;

    // Mark super block location as reserved.
    allocation_set(context, sb_block);

    // Read in the super block from disk.
    ret = read_page(sb, context->bdev, sb_block, false);
    afs_assert(!ret, err, "could not read super block page [%d]", ret);

    // Check for corruption.
    hash_sha256((uint8_t*)sb + SHA256_SZ, sizeof(*sb) - SHA256_SZ, sb_digest);
    ret = memcmp(sb->sb_hash, sb_digest, SHA256_SZ);
    afs_assert_action(!ret, ret = -ENOENT, err, "super block corrupted");

    // Confirm size is same.
    afs_assert_action(context->instance_size == sb->instance_size, ret = -EINVAL, err,
                      "incorrect size provided [%llu:%llu]", context->instance_size, sb->instance_size);

    // TODO: Acquire from RS params in SB.
    build_configuration(context, 4);

    ret = afs_create_map(context);
    afs_assert(!ret, err, "could not create artifice map [%d]", ret);

    ret = afs_fill_map(sb, context);
    afs_assert(!ret, fill_err, "could not fill artifice map [%d]", ret);

    rebuild_allocation_vector(context);
    afs_debug("Artifice map recreated");
    return 0;

fill_err:
    vfree(context->afs_map);

err:
    return ret;
}

/**
 * Acquire a free block from the free list.
 */
uint32_t 
acquire_block(struct afs_passive_fs *fs, struct afs_private *context)
{
    uint32_t block_num;

    for (block_num = 0; block_num < fs->list_len; block_num++) {
        if (allocation_set(context, block_num)) {
            return fs->block_list[block_num];
        }
    }

    return AFS_INVALID_BLOCK;
}

/**
 * Acquire a SHA1 hash of given data.
 * 
 * @digest Array to return digest into. Needs to be pre-allocated 20 bytes.
 */
int 
hash_sha1(const void *data, const uint32_t data_len, uint8_t *digest)
{
    const char *alg_name = "sha1";
    struct crypto_shash *tfm;
    struct shash_desc *desc;
    int ret;
    
    tfm = crypto_alloc_shash(alg_name, 0, CRYPTO_ALG_ASYNC);
    afs_assert_action(!IS_ERR(tfm), ret = PTR_ERR(tfm), tfm_done, "could not allocate tfm [%d]", ret);

    desc = kmalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
    afs_assert_action(desc, ret = -ENOMEM, desc_done, "could not allocate desc [%d]", ret);
    
    desc->tfm = tfm;
    desc->flags = 0;
    ret = crypto_shash_digest(desc, data, data_len, digest);
    afs_assert(!ret, compute_done, "error computing sha1 [%d]", ret);

compute_done:
    kfree(desc);

desc_done:
    crypto_free_shash(tfm);

tfm_done:
    return ret;
}

/**
 * Acquire a SHA256 hash of given data.
 *
 * @digest Array to return digest into. Needs to be pre-allocated 32 bytes.
 */
int 
hash_sha256(const void *data, const uint32_t data_len, uint8_t *digest)
{
    const char *alg_name = "sha256";
    struct crypto_shash *tfm;
    struct shash_desc *desc;
    int ret;
    
    tfm = crypto_alloc_shash(alg_name, 0, CRYPTO_ALG_ASYNC);
    afs_assert_action(!IS_ERR(tfm), ret = PTR_ERR(tfm), tfm_done, "could not allocate tfm [%d]", ret);

    desc = kmalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
    afs_assert_action(desc, ret = -ENOMEM, desc_done, "could not allocate desc [%d]", ret);
    
    desc->tfm = tfm;
    desc->flags = 0;
    ret = crypto_shash_digest(desc, data, data_len, digest);
    afs_assert(!ret, compute_done, "error computing sha256 [%d]", ret);

compute_done:
    kfree(desc);

desc_done:
    crypto_free_shash(tfm);

tfm_done:
    return ret;
}

/**
 * Acquire a SHA512 hash of given data.
 *
 * @digest Array to return digest into. Needs to be pre-allocated 64 bytes.
 */
int 
hash_sha512(const void *data, const uint32_t data_len, uint8_t *digest)
{
    const char *alg_name = "sha512";
    struct crypto_shash *tfm;
    struct shash_desc *desc;
    int ret;
    
    tfm = crypto_alloc_shash(alg_name, 0, CRYPTO_ALG_ASYNC);
    afs_assert_action(!IS_ERR(tfm), ret = PTR_ERR(tfm), tfm_done, "could not allocate tfm [%d]", ret);

    desc = kmalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
    afs_assert_action(desc, ret = -ENOMEM, desc_done, "could not allocate desc [%d]", ret);
    
    desc->tfm = tfm;
    desc->flags = 0;
    ret = crypto_shash_digest(desc, data, data_len, digest);
    afs_assert(!ret, compute_done, "error computing sha512 [%d]", ret);

compute_done:
    kfree(desc);

desc_done:
    crypto_free_shash(tfm);

tfm_done:
    return ret;
}

/**
 * Perform a reverse bit scan for an unsigned long.
 */
inline uint64_t 
bsr(uint64_t n)
{
	__asm__("bsr %1,%0" : "=r" (n) : "rm" (n));
	return n;
}

/**
 * Get a pointer to a map entry in the Artifice map.
 */
static inline uint8_t*
afs_get_map_entry(struct afs_private *context, uint32_t index)
{
    return context->afs_map + (index * context->map_entry_sz);
}

/**
 * Read a block from the map.
 */
static int
__afs_read_block(struct afs_private *context, void *data, uint32_t block) 
{
    struct afs_map_tuple *map_entry_tuple = NULL;
    uint8_t *map_entry = NULL;
    uint8_t *map_entry_hash = NULL;
    uint8_t *map_entry_entropy = NULL;
    uint8_t digest[SHA1_SZ];
    int ret, i;

    map_entry = afs_get_map_entry(context, block);
    map_entry_tuple = (struct afs_map_tuple*)map_entry;
    map_entry_hash = map_entry + (context->num_carrier_blocks * sizeof(*map_entry_tuple));
    map_entry_entropy = map_entry_hash + SHA128_SZ;

    if (map_entry_tuple[0].carrier_block_pointer == AFS_INVALID_BLOCK) {
        // Zero-fill an unmapped block.
        memset(data, 0, AFS_BLOCK_SIZE);
    } else {
        // TODO: Use Reed-Solomon to rebuild block.
        for (i = 0; i < context->num_carrier_blocks; i++) {
            ret = read_page(data, context->bdev, map_entry_tuple[i].carrier_block_pointer, false);
            afs_assert_action(!ret, ret = -EIO, done, "could not read page at block [%u]", map_entry_tuple[i].carrier_block_pointer);
        }

        // Confirm hash matches.
        hash_sha1(data, AFS_BLOCK_SIZE, digest);
        ret = memcmp(map_entry_hash, digest + (SHA1_SZ - SHA128_SZ), SHA128_SZ);
        afs_assert_action(!ret, ret = -ENOENT, done, "data block is corrupted [%u]", block);
    }
    ret = 0;

done:
    return ret;
}

/**
 * Map a read request from userspace.
 */
int afs_read_request(struct afs_private *context, struct bio *bio)
{
    uint8_t *raw_block = NULL;
    uint8_t *user_data = NULL;
    uint32_t req_size;
    uint32_t block_num;
    uint32_t sector_offset;
    int ret;

    block_num = (bio->bi_iter.bi_sector * AFS_SECTOR_SIZE) / AFS_BLOCK_SIZE;
    sector_offset = bio->bi_iter.bi_sector % (AFS_BLOCK_SIZE / AFS_SECTOR_SIZE);
    req_size = bio_sectors(bio) * AFS_SECTOR_SIZE;
    afs_assert_action(req_size <= AFS_BLOCK_SIZE, ret = -EINVAL, done, "cannot handle requested size [%u]", req_size);
    afs_debug("read request [Size: %u | Block: %u | Page Off: %u | Sector Off: %u]", 
              req_size, block_num, bio_offset(bio), sector_offset);

    // Read the raw block.
    raw_block = context->raw_block_read;
    ret = __afs_read_block(context, raw_block, block_num);
    afs_assert(!ret, done, "could not read data block [%d:%u]", ret, block_num);

    // Copy in the part required.
    user_data = (uint8_t*)page_address(bio_page(bio));
    memcpy(user_data + bio_offset(bio), raw_block + (sector_offset * AFS_SECTOR_SIZE), req_size);
    ret = 0;

done:
    return ret;
}

/**
 * Map a write request from userspace.
 */
int afs_write_request(struct afs_private *context, struct bio *bio)
{
    struct afs_map_tuple *map_entry_tuple = NULL;
    uint8_t *map_entry = NULL;
    uint8_t *map_entry_hash = NULL;
    uint8_t *map_entry_entropy = NULL;
    uint8_t *raw_block = NULL;
    uint8_t *user_data = NULL;
    uint8_t digest[SHA1_SZ];
    uint32_t req_size;
    uint32_t block_num;
    uint32_t sector_offset;
    int ret = 0, i;

    user_data = (uint8_t*)page_address(bio_page(bio));
    raw_block = context->raw_block_write;
    sector_offset = bio->bi_iter.bi_sector % (AFS_BLOCK_SIZE / AFS_SECTOR_SIZE);
    block_num = (bio->bi_iter.bi_sector * AFS_SECTOR_SIZE) / AFS_BLOCK_SIZE;
    req_size = bio_sectors(bio) * AFS_SECTOR_SIZE;
    afs_assert_action(req_size <= AFS_BLOCK_SIZE, ret = -EINVAL, err, "cannot handle requested size [%u]", req_size);

    map_entry = afs_get_map_entry(context, block_num);
    map_entry_tuple = (struct afs_map_tuple*)map_entry;
    map_entry_hash = map_entry + (context->num_carrier_blocks * sizeof(*map_entry_tuple));
    map_entry_entropy = map_entry_hash + SHA128_SZ;
    afs_debug("write request [Size: %u | Block: %u | Page Off: %u | Sector Off: %u]", 
              req_size, block_num, bio_offset(bio), sector_offset);

    // TODO: Acquire shards of this data block.

    memset(raw_block, 0, AFS_BLOCK_SIZE);
    if (map_entry_tuple[0].carrier_block_pointer != AFS_INVALID_BLOCK) {
        // Re-use the blocks if it is a modification of a block.
        // Read-modify-write.
        ret = __afs_read_block(context, raw_block, block_num);
        afs_assert(!ret, err, "could not read data block [%d:%u]", ret, block_num);
        memcpy(raw_block + (sector_offset * AFS_SECTOR_SIZE), user_data + bio_offset(bio), req_size);

        for (i = 0; i < context->num_carrier_blocks; i++) {
            ret = write_page(raw_block, context->bdev, map_entry_tuple[i].carrier_block_pointer, false);
            afs_assert_action(!ret, ret = -EIO, reset_entry, "could not write page at block [%u]", map_entry_tuple[i].carrier_block_pointer);
        }
    } else {
        // Writing new blocks.
        memcpy(raw_block + (sector_offset * AFS_SECTOR_SIZE), user_data + bio_offset(bio), req_size);

        for (i = 0; i < context->num_carrier_blocks; i++) {
            block_num = acquire_block(&context->passive_fs, context);
            afs_assert_action(block_num != AFS_INVALID_BLOCK, ret = -ENOSPC, reset_entry, "no free space left");
            map_entry_tuple[i].carrier_block_pointer = block_num;
            ret = write_page(raw_block, context->bdev, block_num, false);
            afs_assert_action(!ret, ret = -EIO, reset_entry, "could not write page at block [%u]", block_num);
        }
    }

    // TODO: Set the entropy hash correctly.
    hash_sha1(raw_block, AFS_BLOCK_SIZE, digest);
    memcpy(map_entry_hash, digest + (SHA1_SZ - SHA128_SZ), SHA128_SZ);
    memset(map_entry_entropy, 0, ENTROPY_HASH_SZ);
    return 0;

reset_entry:
    for (i = 0; i < context->num_carrier_blocks; i++) {
        if (map_entry_tuple[i].carrier_block_pointer != AFS_INVALID_BLOCK) {
            allocation_free(context, map_entry_tuple[i].carrier_block_pointer);
        }
        map_entry_tuple[i].carrier_block_pointer = AFS_INVALID_BLOCK;
    }

err:
    return ret;
}