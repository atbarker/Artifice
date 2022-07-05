/*
 * Author: Yash Gupta <ygupta@ucsc.edu>, Austen Barker <atbarker@ucsc.edu>
 * Copyright: UC Santa Cruz, SSRC
 */
#include <dm_afs.h>
#include <dm_afs_engine.h>
#include <dm_afs_io.h>
#include <dm_afs_modules.h>
#include <linux/delay.h>
#include <linux/vmalloc.h>
#include "lib/cauchy_rs.h"
#include "lib/sha3.h"

/**
 * A procedure to detect the existing file system on a block
 * device or a block device partition.
 * 
 * We read the first 4KB and pass that to each detection
 * function. The first 4KB _should_ be enough for most
 * file systems, but in case it is not, the detection
 * functions are free to read in more data. In that case
 * the 4KB acts as a cache.
 * 
 * @device  Block device to look at.
 * @return  FS_XXXX/FS_ERR.
 */
static int8_t
detect_fs(struct block_device *device, struct afs_passive_fs *fs)
{
    uint8_t *page = NULL;
    int8_t ret;

    page = kmalloc(AFS_BLOCK_SIZE, GFP_KERNEL);
    afs_action(page, ret = -ENOMEM, alloc_err, "could not allocate page [%d]", ret);
    ret = read_page(page, device, 0, 0, false);
    afs_assert(!ret, read_err, "could not read page [%d]", ret);

    // Add more detection functions as a series of else..if blocks.
    // TODO: add afs_shadow_detect...
    // TODO: modify passive block device size (config.bdev_size) to exclude reserved block so those are removed from the allocation vector

    if (afs_fat32_detect(page, device, fs)) {
        ret = FS_FAT32;
    } else if (afs_ext4_detect(page, device, fs)) {
        ret = FS_EXT4;
    } else if (afs_ntfs_detect(page, device, fs)) {
        ret = FS_NTFS;
    } else if (afs_shadow_detect(page, device, fs)){
        ret = FS_SHADOW;
    } else {
        ret = FS_ERR;
    }
    kfree(page);

    afs_debug("detected %d", ret);
    return ret;

read_err:
    kfree(page);

alloc_err:
    return FS_ERR;
}

/**
 * Parse the supplied arguments from the user.
 * 
 * Adding a '-1' to all sizes to make sure we
 * keep a NULL.
 */
static int32_t
parse_afs_args(struct afs_args *args, unsigned int argc, char *argv[])
{
    const uint32_t BASE_10 = 10;
    const int8_t TYPE = 0;
    const int8_t PASSPHRASE = 1;
    const int8_t DISK = 2;
    int i;

    afs_assert(argc >= 3, err, "not enough arguments");
    memset(args, 0, sizeof(*args));

    // These three are always required.
    afs_assert(!kstrtou8(argv[TYPE], BASE_10, &args->instance_type), err, "instance type not integer");
    strncpy(args->passphrase, argv[PASSPHRASE], PASSPHRASE_SZ - 1);
    strncpy(args->passive_dev, argv[DISK], PASSIVE_DEV_SZ - 1);
    afs_debug("Type: %d", args->instance_type);
    afs_debug("Passphrase: %s", args->passphrase);
    afs_debug("Device: %s", args->passive_dev);

    // These may be optional depending on the type.
    // TODO: Add long parameter for reed-solomon configuration.
    for (i = DISK + 1; i < argc; i++) {
        if (!strcmp(argv[i], "--entropy")) {
            afs_assert(++i < argc, err, "missing value [entropy source]");
            strncpy(args->entropy_dir, argv[i], ENTROPY_DIR_SZ - 1);
        } else if (!strcmp(argv[i], "--shadow_passphrase")) {
            afs_assert(++i < argc, err, "missing value [shadow passphrase]");
            strncpy(args->shadow_passphrase, argv[i], PASSPHRASE_SZ - 1);
        } else {
            afs_assert(0, err, "unknown argument");
        }
    }
    afs_debug("Entropy: %s", args->entropy_dir);
    afs_debug("Shadow Passphrase: %s", args->shadow_passphrase);

    // Now that we have all the arguments, we need to make sure
    // that they semantically make sense.
    switch (args->instance_type) {
    case TYPE_CREATE:
        afs_assert(args->entropy_dir[0] != 0, err, "entropy source not provided");
        afs_assert(args->shadow_passphrase[0] == 0, err, "shadow passphrase provided");
        break;

    case TYPE_MOUNT:
        afs_assert(args->entropy_dir[0] == 0, err, "entropy source provided");
        afs_assert(args->shadow_passphrase[0] == 0, err, "shadow passphrase provided");
        break;

    case TYPE_SHADOW:
        afs_assert(args->entropy_dir[0] != 0, err, "entropy source not provided");
        afs_assert(args->shadow_passphrase[0] != 0, err, "shadow passphrase not provided");
        break;

    default:
        afs_assert(0, err, "unknown type of instance chosen");
    }
    return 0;

err:
    return -EINVAL;
}

/**
 * Flight queue.
 */
static void
afs_flightq(struct work_struct *ws)
{
    struct afs_map_request *req = NULL;
    int ret = 0;

    req = container_of(ws, struct afs_map_request, req_ws);

    if(work_pending(ws)){
        return;
    }

    switch (bio_op(req->bio)) {
    case REQ_OP_READ:
        //if ((existing_req = afs_eq_req_exist(req->eq, req->bio))) {
        //    memcpy(req->data_block, existing_req->data_block, AFS_BLOCK_SIZE);
        //    afs_req_clean(req);
        //    return;
        //} else {
            atomic64_set(&req->state, REQ_STATE_FLIGHT);
            ret = afs_read_request(req, req->bio);
        //}
        break;

    case REQ_OP_WRITE:
        //afs_eq_add(req->eq, req);
        atomic64_set(&req->state, REQ_STATE_FLIGHT);
        ret = afs_write_request(req, req->bio);
        break;

    default:
        ret = -EINVAL;
        afs_debug("This case should never be encountered!");
    }
    afs_assert(!ret, done, "could not perform operation [%d:%d]", ret, bio_op(req->bio));
    return;

done:
    afs_req_clean(req);
    return;
}

/**
 * Work queue for the rebuild operation
 * Iterates through all the blocks 
 */
static void
afs_rebuildq(struct work_struct *ws)
{
    struct afs_map_request *req = NULL;
    int ret = 0;

    req = container_of(ws, struct afs_map_request, req_ws);

    if(work_pending(ws)){
        return;
    }

    atomic64_set(&req->state, REQ_STATE_FLIGHT);
    ret = afs_rebuild_request(req);

    afs_assert(!ret, done, "could not perform operation [%d:%d]", ret, bio_op(req->bio));
    return;

done:
    afs_req_clean(req);
    return;
}

void
afs_cryptoq(struct work_struct *ws){
    struct afs_map_request *req = NULL;
    int ret = 0;
    req = container_of(ws, struct afs_map_request, req_ws);

    if(work_pending(ws)){
        return;
    }
    ret = afs_read_decode(req);

    afs_assert(!ret, done, "could not perform operation [%d:%d]", ret, bio_op(req->bio));
    return;

done:
    afs_req_clean(req);
    return;

}

/**
 * Allocate a new bio and copy in the contents from another
 * one.
 * 
 * NO-OP for read requests.
 */
static struct bio *
__clone_bio(struct bio *bio_src, uint8_t **allocated_page, bool end_bio_src)
{
    // TODO: Override is enabled because without it, there is too much
    // lock contention, leading to overall worse performance.
    const int override = 1;

    struct bio_vec bv;
    struct bvec_iter iter;
    struct bio *bio_ret = NULL;
    uint8_t *page = NULL;
    uint8_t *bio_data = NULL;
    uint32_t sector_offset;
    uint32_t segment_offset;
    uint32_t req_size;

    // Read requests are not cloned.
    if (bio_op(bio_src) == REQ_OP_READ || override) {
        return bio_src;
    }

    ////bio_ret = bio_alloc(GFP_NOIO, 1);
    bio_ret = bio_alloc(bio_src->bi_bdev,1,bio_src->bi_opf,GFP_NOIO);
    
    afs_assert(!IS_ERR(bio_ret), alloc_err, "could not allocate bio [%ld]", PTR_ERR(bio_ret));

    page = kmalloc(AFS_BLOCK_SIZE, GFP_KERNEL);
    afs_assert(page, page_err, "could not allocate page [%d]", -ENOMEM);

    // Copy in the data from the segments.
    sector_offset = bio_src->bi_iter.bi_sector % (AFS_BLOCK_SIZE / AFS_SECTOR_SIZE);
    req_size = bio_sectors(bio_src) * AFS_SECTOR_SIZE;
    afs_assert(req_size <= AFS_BLOCK_SIZE, size_err, "cannot handle requested size [%u]", req_size);

    segment_offset = 0;
    bio_for_each_segment (bv, bio_src, iter) {
        bio_data = kmap(bv.bv_page);
        if (bv.bv_len <= (req_size - segment_offset)) {
            memcpy(page + (sector_offset * AFS_SECTOR_SIZE) + segment_offset, bio_data + bv.bv_offset, bv.bv_len);
        } else {
            memcpy(page + (sector_offset * AFS_SECTOR_SIZE) + segment_offset, bio_data + bv.bv_offset, req_size - segment_offset);
            kunmap(bv.bv_page);
            break;
        }
        segment_offset += bv.bv_len;
        kunmap(bv.bv_page);
    }

    bio_add_page(bio_ret, virt_to_page(page), req_size, sector_offset * AFS_SECTOR_SIZE);
    ////bio_ret->bi_opf = bio_src->bi_opf;
    bio_ret->bi_iter.bi_sector = bio_src->bi_iter.bi_sector;
    bio_ret->bi_iter.bi_size = req_size;
    bio_ret->bi_iter.bi_idx = 0;
    bio_ret->bi_iter.bi_bvec_done = 0;

    // End bio if specified.
    if (end_bio_src) {
        bio_endio(bio_src);
    }
    *allocated_page = page;

    return bio_ret;

size_err:
    kfree(page);

page_err:
    bio_endio(bio_ret);

alloc_err:
    return NULL;
}

/**
 * Initialize an afs_map_request struct.
 * Returns NULL on error.
 *
 * @context: AFS private context struct.
 * 
 * @return afs_map_request struct pointer.
 */
static struct afs_map_request*
init_request(struct afs_private *context) {
    struct afs_map_request *req;
    int i;

    req = kmalloc(sizeof(*req), GFP_KERNEL);
    if(req == NULL){
        return req;
    }
    
    req->afs_context = context;
    req->bdev = context->bdev;
    req->map = context->afs_map;
    req->config = &context->config;
    req->fs = &context->passive_fs;
    req->vector = &context->vector;
    req->allocated_write_page = NULL;
    req->encoder = NULL;
    req->num_erasures = 0;
    req->encoding_type = context->encoding_type;
    memcpy(req->iv, context->passphrase_hash, 16);
    atomic_set(&req->rebuild_flag, 0);
    spin_lock_init(&req->req_lock);
    atomic64_set(&req->state, REQ_STATE_GROUND);
    for(i = 0; i < req->config->num_carrier_blocks; i++) {
        req->carrier_blocks[i] = (uint8_t*)__get_free_page(GFP_KERNEL);
    }

    return req;
}

static void print_bio_info(struct bio *bio){
    struct bio_vec bv;
    struct bvec_iter iter;
    uint32_t segment_offset;
    segment_offset = 0;
    //afs_debug("bio sectors (%d), sector offset (%lu), alignment off (%lu)", bio_sectors(bio), bio->bi_iter.bi_sector, AFS_SECTORS_PER_BLOCK - (bio->bi_iter.bi_sector % 8));
    bio_for_each_segment (bv, bio, iter) {
        if(bv.bv_len != 4096 || bv.bv_offset != 0 || bio_sectors(bio) != 8){
            afs_debug("bio sectors (%d), sector offset (%lu), alignment off (%lu)", bio_sectors(bio), bio->bi_iter.bi_sector, AFS_SECTORS_PER_BLOCK - (bio->bi_iter.bi_sector % 8));
            afs_debug("bv.bv_len (%d), bv.bv_offset %d", bv.bv_len, bv.bv_offset);
        }
    }
}

/**
 * Iterate through the Map and rebuild blocks one by one
 */
static int
afs_rebuild(struct dm_target *ti){
    int i = 0;
    struct afs_private *context = ti->private;
    struct afs_map_request *req = NULL;

    for(i = 0; i < context->config.num_blocks; i++){
        req = init_request(context);
        req->block = i;
	req->sector_offset = 0;
	req->request_size = AFS_BLOCK_SIZE;

	req->eq = &context->rebuild_eq;
	INIT_WORK(&req->req_ws, afs_rebuildq);
	queue_work(context->rebuild_wq, &req->req_ws);
    }
    return 0;
};

/**
 * Map function for this target. This is the heart and soul
 * of the device mapper. We receive block I/O requests which
 * we need to remap to our underlying device and then submit
 * the request. This function is essentially called for any I/O 
 * on a device for this target.
 * 
 * The map function is called extensively for each I/O
 * issued upon the device mapper target. For performance 
 * consideration, the map function is verbose only for debug builds.
 * 
 * @ti  Target instance for the device.
 * @bio The block I/O request to be processed.
 * 
 * @return  device-mapper code
 *  DM_MAPIO_SUBMITTED: dm_afs has submitted the bio request.
 *  DM_MAPIO_REMAPPED:  dm_afs has remapped the request and device-mapper
 *                      needs to submit it.
 *  DM_MAPIO_REQUEUE:   dm_afs encountered a problem and the bio needs to
 *                      be resubmitted.
 */
static int
afs_map(struct dm_target *ti, struct bio *bio) {
    const int override = 0;

    struct afs_private *context = ti->private;
    struct afs_map_request *req = NULL;
    uint32_t sector_offset;
    uint32_t max_sector_count;
    int ret;


    // We only support bio's with a maximum length of 8 sectors (4KB).
    // Moreover, we only support processing a single block per bio.
    // Hence, a request such as (length: 4KB, sector: 1) crosses
    // a block boundary and involves two blocks. For all such requests,
    // we truncate the bio to within the single page.

    sector_offset = bio->bi_iter.bi_sector % (AFS_SECTORS_PER_BLOCK);
    max_sector_count = (AFS_SECTORS_PER_BLOCK) - sector_offset;
    if (bio_sectors(bio) > max_sector_count) {
        dm_accept_partial_bio(bio, max_sector_count);
    }

    if (override) {
        bio_set_dev(bio, context->bdev);
        print_bio_info(bio);
        submit_bio(bio);
        return DM_MAPIO_SUBMITTED;
    }

    req = init_request(context);
    afs_action(req, ret = DM_MAPIO_KILL, done, "could not allocate memory for request");

    switch (bio_op(bio)) {
    case REQ_OP_READ:
    case REQ_OP_WRITE:
        req->bio = __clone_bio(bio, &req->allocated_write_page, true);
        afs_action(req->bio, ret = DM_MAPIO_KILL, done, "could not clone bio");

        req->block = bio->bi_iter.bi_sector / AFS_SECTORS_PER_BLOCK;
        req->sector_offset = bio->bi_iter.bi_sector % (AFS_BLOCK_SIZE / AFS_SECTOR_SIZE);
        req->request_size = bio_sectors(bio) * AFS_SECTOR_SIZE;

        req->eq = &context->flight_eq;
        INIT_WORK(&req->req_ws, afs_flightq);
        queue_work(context->flight_wq, &req->req_ws);

        ret = DM_MAPIO_SUBMITTED;
        break;

    case REQ_OP_FLUSH:
        while (afs_eq_req_exist(&context->flight_eq, bio)) {
            afs_debug("stuck waiting for flush");
            msleep(1);
        }
        bio_endio(bio);
        ret = DM_MAPIO_SUBMITTED;
        break;

    default:
        afs_debug("unknown operation");
        ret = DM_MAPIO_KILL;
    }

done:
    if (ret == DM_MAPIO_KILL) {
        bio_endio(bio);
        if (req) {
            kfree(req);
        }
    }
    return ret;
}

/**
 * Constructor function for this target. The constructor
 * is called for each new instance of a device for this
 * target. To create a new device, 'dmsetup create' is used.
 * 
 * The constructor is used to parse the program arguments
 * and detect the file system in effect on the passive
 * block device.
 * 
 * It also reads all the files in the entropy directory and
 * creates a hash table of the files, keyed by an 8 byte
 * hash.
 * TODO: ^Implement.
 * 
 * @ti Target instance for new device.
 * @return 0  New device instance successfully created.
 * @return <0 Error.
 */
static int
afs_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
    struct afs_private *context = NULL;
    struct afs_args *args = NULL;
    struct afs_passive_fs *fs = NULL;
    struct afs_super_block *sb = NULL;
    int ret;
    int8_t detected_fs;
    uint64_t instance_size;

    // Make sure instance is large enough.
    afs_debug("dm target length: %lu", ti->len);
    instance_size = ti->len * AFS_SECTOR_SIZE;
    afs_action(instance_size >= AFS_MIN_SIZE, ret = -EINVAL, err, "instance too small [%llu]", instance_size);

    // Confirm our structure sizes.
    afs_action(sizeof(*sb) == AFS_BLOCK_SIZE, ret = -EINVAL, err, "super block structure incorrect size [%lu]", sizeof(*sb));
    afs_action(sizeof(struct afs_ptr_block) == AFS_BLOCK_SIZE, ret = -EINVAL, err, "pointer block structure incorrect size [%lu]", sizeof(struct afs_ptr_block));

    context = kmalloc(sizeof(*context), GFP_KERNEL);
    afs_action(context, ret = -ENOMEM, err, "kmalloc failure [%d]", ret);
    memset(context, 0, sizeof(*context));
    context->config.instance_size = instance_size;

    // Parge instance arguments.
    args = &context->args;
    ret = parse_afs_args(args, argc, argv);
    sha3_256((uint8_t*)args->passphrase, PASSPHRASE_SZ, context->passphrase_hash);
    afs_assert(!ret, args_err, "unable to parse arguments");
    //TODO, set this as a command line option
    context->encoding_type = AONT_RS;

    // Acquire the block device based on the args. This gives us a
    // wrapper on top of the kernel block device structure.
    ret = dm_get_device(ti, args->passive_dev, dm_table_get_mode(ti->table), &context->passive_dev);
    afs_assert(!ret, args_err, "could not find given disk [%s]", args->passive_dev);
    context->bdev = context->passive_dev->bdev;
    //They changed how you get sector size, eliminating hd_struct *bd_part from the bdev struct, so we now have a helper
#if LINUX_VERSION_CODE > KERNEL_VERSION(5,10,78)
    context->config.bdev_size = bdev_nr_sectors(context->bdev);
    afs_debug("block device size, %llu", context->config.bdev_size);
#else
    context->config.bdev_size = context->bdev->bd_part->nr_sects;
#endif

    fs = &context->passive_fs;
    detected_fs = detect_fs(context->bdev, fs);
    switch (detected_fs) {
    case FS_FAT32:
        afs_debug("detected FAT32");
        break;

    case FS_NTFS:
        afs_debug("detected NTFS");
        break;

    case FS_EXT4:
        afs_debug("detected EXT4");
        break;

    case FS_ERR:
        // TODO: Change.
        // afs_action(0, ret = -ENOENT, fs_err, "unknown file system");
        break;

    default:
        afs_action(0, ret = -ENXIO, fs_err, "seems like all hell broke loose");
    }

    // This is all just for testing.
    // BEGIN.
    //fs->block_list = vmalloc(1048576 * sizeof(*fs->block_list));
    //fs->list_len = 1048576;

    //for (i = 0; i < 100; i++) {
    //    afs_debug("Allocation list: %d ", fs->block_list[i]);
    //}
    // END.

    // Allocate the free list allocation vector to be able
    // to map all possible blocks and mask the invalid block.
    context->vector.vector = bit_vector_create((uint64_t)U32_MAX);
    afs_action(context->vector.vector, ret = -ENOMEM, vec_err, "could not allocate allocation vector");
    spin_lock_init(&context->vector.lock);
    allocation_set(&context->vector, AFS_INVALID_BLOCK);

    sb = &context->super_block;
    switch (args->instance_type) {
    case TYPE_CREATE:
        // TODO: Acquire carrier block count from RS parameters.
        build_configuration(context, 4, 1);
        ret = write_super_block(sb, fs, context);
        afs_assert(!ret, sb_err, "could not write super block [%d]", ret);
        break;

    case TYPE_MOUNT:
        ret = find_super_block(sb, context);
        afs_assert(!ret, sb_err, "could not find super block [%d]", ret);
        break;

    case TYPE_SHADOW:
        // Create nested instance.
        break;
    }
    afs_debug("List length %d", fs->list_len);
    // We are now ready to process map requests.
    //afs_action(!IS_ERR(context->ground_wq), ret = PTR_ERR(context->ground_wq), gwq_err, "could not create gwq [%d]", ret);

    context->flight_wq = alloc_workqueue("%s", WQ_UNBOUND | WQ_HIGHPRI | WQ_CPU_INTENSIVE | WQ_MEM_RECLAIM, num_online_cpus(), "Artifice Flight WQ");
    context->rebuild_wq = alloc_workqueue("%s", WQ_UNBOUND | WQ_HIGHPRI | WQ_CPU_INTENSIVE | WQ_MEM_RECLAIM, 2, "Artifice Rebuild WQ");
    context->crypto_wq = alloc_workqueue("%s", WQ_UNBOUND | WQ_HIGHPRI | WQ_CPU_INTENSIVE | WQ_MEM_RECLAIM, num_online_cpus(), "Artifice Crypt WQ");
    //context->flight_wq = alloc_ordered_workqueue("%s", WQ_HIGHPRI, "Artifice Flight WQ");
    afs_action(!IS_ERR(context->flight_wq), ret = PTR_ERR(context->flight_wq), fwq_err, "could not create fwq [%d]", ret);
    afs_action(!IS_ERR(context->rebuild_wq), ret = PTR_ERR(context->rebuild_wq), fwq_err, "could not create rebuild wq [%d]", ret);
    afs_action(!IS_ERR(context->crypto_wq), ret = PTR_ERR(context->crypto_wq), fwq_err, "could not create crypto wq [%d]", ret);

    afs_eq_init(&context->flight_eq);
    afs_eq_init(&context->rebuild_eq);

    afs_debug("constructor completed");
    ti->private = context;

    //Start the repair process if we are mounting an existing volume
    if(args->instance_type == TYPE_MOUNT){
        afs_rebuild(ti);
    }
    return 0;

fwq_err:
    kfree(context->afs_ptr_blocks);
    vfree(context->afs_map);

sb_err:
    bit_vector_free(context->vector.vector);

vec_err:
    vfree(fs->block_list);

fs_err:
    dm_put_device(ti, context->passive_dev);

args_err:
    kfree(context);

err:
    return ret;
}

/**
 * Destructor function for this target. The destructor
 * is called when a device instance for this target is
 * destroyed.
 *
 * @ti  Target instance to be destroyed.
 */
static void
afs_dtr(struct dm_target *ti)
{
    struct afs_private *context = ti->private;
    int err;

    // Wait for all requests to have processed. DO NOT busy wait.
    while(!afs_eq_empty(&context->flight_eq) && !afs_eq_empty(&context->rebuild_eq)) {
        msleep(1);
    }

    // Update the Artifice map on the disk.
    err = afs_create_map_blocks(context);
    if (err) {
        afs_alert("could not create Artifice map blocks [%d]", err);
    } else {
        err = write_map_blocks(context, true);
        if (err) {
            afs_alert("could not update Artifice map on disk [%d]", err);
        }
        vfree(context->afs_map_blocks);
    }
    

    // Free the Artifice pointer blocks.
    kfree(context->afs_ptr_blocks);

    // Free the Artifice map.
    vfree(context->afs_map);

    // Free the bit vector allocation.
    bit_vector_free(context->vector.vector);

    // Free the block list for the passive FS.
    vfree(context->passive_fs.block_list);

    // Put the device back.
    dm_put_device(ti, context->passive_dev);

    // Destroy the workqueues.
    destroy_workqueue(context->flight_wq);
    destroy_workqueue(context->rebuild_wq);
    destroy_workqueue(context->crypto_wq);

    // Free storage used by context.
    kfree(context);
    afs_debug("destructor completed");
}

/** ----------------------------------------------------------- DO-NOT-CROSS ------------------------------------------------------------------- **/

static struct target_type afs_target = {
    .name = DM_AFS_NAME,
    .version = { DM_AFS_MAJOR_VER, DM_AFS_MINOR_VER, DM_AFS_PATCH_VER },
    .module = THIS_MODULE,
    .ctr = afs_ctr,
    .dtr = afs_dtr,
    .map = afs_map
};

/**
 * Initialization function called when the module
 * is inserted dynamically into the kernel. It registers
 * the dm_afs target into the device-mapper tree.
 * 
 * @return  0   Target registered, no errors.
 * @return  <0  Target registration failed.
 */
static __init int
afs_init(void)
{
    int ret;

    ret = dm_register_target(&afs_target);
    afs_assert(ret >= 0, done, "registration failed [%d]", ret);
    afs_debug("registration successful");

done:
    return ret;
}

/**
 * Destructor function called when module is removed
 * from the kernel. This function means nothing when the
 * module is statically linked into the kernel.
 * 
 * Unregisters the dm_afs target from the device-mapper
 * tree.
 */
static void
afs_exit(void)
{
    dm_unregister_target(&afs_target);
    afs_debug("unregistered dm_afs");
}

module_init(afs_init);
module_exit(afs_exit);
MODULE_AUTHOR("Yash Gupta, Austen Barker");
MODULE_LICENSE("GPL");

// Module parameters.
int afs_debug_mode = 1;
module_param(afs_debug_mode, int, 0644);
MODULE_PARM_DESC(afs_debug_mode, "Set to 1 to enable debug mode {may affect performance}");

/** -------------------------------------------------------------------------------------------------------------------------------------------- **/
