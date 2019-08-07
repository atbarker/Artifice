/*
 * Author: Yash Gupta <ygupta@ucsc.edu>, Austen Barker <atbarker@ucsc.edu>
 * Copyright: UC Santa Cruz, SSRC
 */
#include <dm_afs.h>
#include <dm_afs_engine.h>
#include <dm_afs_io.h>
#include <dm_afs_modules.h>
#include <linux/delay.h>
#include "lib/cauchy_rs.h"

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
 * Clean queue.
 * 
 * This is scheduled on the global kernel workqueue. Simply
 * removes all elements which have finished processing.
 */
static void
afs_cleanq(struct work_struct *ws)
{
    struct afs_private *context = NULL;
    struct afs_engine_queue *flight_eq = NULL;
    struct afs_map_queue *node = NULL, *node_extra = NULL;
    long long state;

    context = container_of(ws, struct afs_private, clean_ws);
    flight_eq = &context->flight_eq;

    spin_lock(&flight_eq->mq_lock);
    list_for_each_entry_safe (node, node_extra, &flight_eq->mq.list, list) {
        state = atomic64_read(&node->req.state);
        if (state == REQ_STATE_COMPLETED) {
            list_del(&node->list);
            kfree(node);
        }
    }
    spin_unlock(&flight_eq->mq_lock);
}

/**
 * Flight queue.
 */
static void
afs_flightq(struct work_struct *ws)
{
    struct afs_map_queue *element = NULL;
    struct afs_map_request *req = NULL;
    int ret = 0;

    element = container_of(ws, struct afs_map_queue, req_ws);
    req = &element->req;

    spin_lock(&req->req_lock);
    if(atomic_read(&req->pending) != 0){
        afs_debug("already processing");
        return;
    }
    
    switch (bio_op(req->bio)) {
    case REQ_OP_READ:
        atomic_set(&req->pending, 1);
        ret = afs_read_request(req, req->bio);
        break;

    case REQ_OP_WRITE:
        atomic_set(&req->pending, 1);
        ret = afs_write_request(req, req->bio);
        break;

    default:
        ret = -EINVAL;
        afs_debug("This case should never be encountered!");
    }
    //atomic64_set(&req->state, REQ_STATE_COMPLETED);
    if(atomic_read(&req->pending) == 2){
        goto done;
    }
    afs_assert(!ret, done, "could not perform operation [%d:%d]", ret, bio_op(req->bio));
    spin_unlock(&req->req_lock);
    return;

done:
    atomic64_set(&req->state, REQ_STATE_COMPLETED);
    bio_endio(req->bio);

    // Write requests may have an allocated page with them. This needs
    // to be free'd AFTER the bio has been ended.
    if (req->allocated_write_page) {
        kfree(req->allocated_write_page);
    }

    schedule_work(element->clean_ws);
}

/**
 * Ground queue.
 * 
 * If the queue is empty, then we have nothing more to do.
 * If the queue is not empty, make sure the request is not
 * contended, and then schedule it.
 */
static void
afs_groundq(struct work_struct *ws)
{
    struct afs_private *context = NULL;
    struct afs_engine_queue *ground_eq = NULL;
    struct afs_engine_queue *flight_eq = NULL;
    struct afs_map_queue *element = NULL;
    struct afs_map_queue *node = NULL, *node_extra = NULL;
    bool exists;

    context = container_of(ws, struct afs_private, ground_ws);
    ground_eq = &context->ground_eq;
    flight_eq = &context->flight_eq;

    while (!afs_eq_empty(ground_eq)) {
        element = NULL;

        // TODO: Possible incorrect ordering.
        // Suppose that we have two requests for the same block number.
        // The request which we come across first is the first request to
        // arrive since we add requests to the tail. It is possible that
        // when we check the first request, there is contention and hence
        // that request is not considered. However, when going through the
        // rest of the requests, we come across the second request to that
        // block number and now that block number is no longer contended.
        // In this case, we will go ahead and process this request, thereby
        // processing a request which came later first. This may result in
        // data corruption, although the likelihood is quite low.

        spin_lock(&ground_eq->mq_lock);
        list_for_each_entry_safe (node, node_extra, &ground_eq->mq.list, list) {
            exists = afs_eq_req_exist(flight_eq, node->req.bio);
            if (!exists) {
                element = node;
                list_del(&node->list);
                break;
            }
        }
        spin_unlock(&ground_eq->mq_lock);

        // If 'element' is not initializied, then we could not
        // find an element to process due to contention for the
        // block number. Sleep and try again.

        if (!element) {
            msleep(1);
            continue;
        }

        // Initialize the element for the flight workqueue.
        element->eq = flight_eq;
        element->clean_ws = &context->clean_ws;
        INIT_WORK(&element->req_ws, afs_flightq);

        atomic64_set(&element->req.state, REQ_STATE_FLIGHT);
        afs_eq_add(flight_eq, element);
        queue_work(context->flight_wq, &element->req_ws);
    }

    // TODO: Potential race condition.
    // Scenario: We find that the ground queue was empty, and we escape the while
    // loop, and get rescheduled at this point.
    // If at this time a new request comes in, 'queue_work' within 'afs_map' might
    // see that we are still on the queue, and hence, not queue 'ground_ws' again.
    // This means we won't be called to process the new request and may miss it until
    // the next request queues us.
    //
    // NOTE: I am not sure if this is actually true. If workqueues remove the work
    // struct when they call the work struct function, then there is no problem since
    // 'ground_ws' will successfully be queued during afs_groundq's execution.
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

    bio_ret = bio_alloc(GFP_NOIO, 1);
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
    bio_ret->bi_opf = bio_src->bi_opf;
    bio_ret->bi_iter.bi_sector = bio_src->bi_iter.bi_sector;
    bio_ret->bi_iter.bi_size = req_size;
    bio_ret->bi_iter.bi_idx = 0;
    bio_ret->bi_iter.bi_done = 0;
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
afs_map(struct dm_target *ti, struct bio *bio)
{
    const int override = 0;

    struct afs_private *context = ti->private;
    struct afs_map_queue *map_element = NULL;
    struct afs_map_request *req = NULL;
    uint32_t sector_offset;
    uint32_t max_sector_count;
    int ret;

    // Bypass Artifice completely.
    if (override) {
        bio_set_dev(bio, context->bdev);
        submit_bio(bio);
        return DM_MAPIO_SUBMITTED;
    }

    // We only support bio's with a maximum length of 8 sectors (4KB).
    // Moreover, we only support processing a single block per bio.
    // Hence, a request such as (length: 4KB, sector: 1) crosses
    // a block boundary and involves two blocks. For all such requests,
    // we truncate the bio to within the single page.

    sector_offset = bio->bi_iter.bi_sector % (AFS_BLOCK_SIZE / AFS_SECTOR_SIZE);
    max_sector_count = (AFS_BLOCK_SIZE / AFS_SECTOR_SIZE) - sector_offset;
    if (bio_sectors(bio) > max_sector_count) {
        dm_accept_partial_bio(bio, max_sector_count);
    }

    // Build request.
    map_element = kmalloc(sizeof(*map_element), GFP_KERNEL);
    afs_action(map_element, ret = DM_MAPIO_KILL, done, "could not allocate memory for request");

    req = &map_element->req;
    req->bdev = context->bdev;
    req->map = context->afs_map;
    req->config = &context->config;
    req->fs = &context->passive_fs;
    req->vector = &context->vector;
    req->allocated_write_page = NULL;
    atomic_set(&req->pending, 0);
    spin_lock_init(&req->req_lock);
    atomic64_set(&req->state, REQ_STATE_GROUND);

    switch (bio_op(bio)) {
    case REQ_OP_READ:
    case REQ_OP_WRITE:
        req->bio = __clone_bio(bio, &req->allocated_write_page, true);
        afs_action(req->bio, ret = DM_MAPIO_KILL, done, "could not clone bio");
        afs_eq_add(&context->ground_eq, map_element);

        // Defer bio to be completed later.
        queue_work(context->ground_wq, &context->ground_ws);
        ret = DM_MAPIO_SUBMITTED;
        break;

    case REQ_OP_FLUSH:
        while (afs_eq_req_exist(&context->flight_eq, bio) || afs_eq_req_exist(&context->ground_eq, bio)) {
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
        if (map_element) {
            kfree(map_element);
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
    //uint64_t i;

    // Make sure instance is large enough.
    instance_size = ti->len * AFS_SECTOR_SIZE;
    afs_action(instance_size >= AFS_MIN_SIZE, ret = -EINVAL, err, "instance too small [%llu]", instance_size);

    // Confirm our structure sizes.
    afs_action(sizeof(*sb) == AFS_BLOCK_SIZE, ret = -EINVAL, err, "super block structure incorrect size [%lu]", sizeof(*sb));
    afs_action(sizeof(struct afs_ptr_block) == AFS_BLOCK_SIZE, ret = -EINVAL, err, "pointer block structure incorrect size");

    context = kmalloc(sizeof(*context), GFP_KERNEL);
    afs_action(context, ret = -ENOMEM, err, "kmalloc failure [%d]", ret);
    memset(context, 0, sizeof(*context));
    context->config.instance_size = instance_size;

    // Parge instance arguments.
    args = &context->args;
    ret = parse_afs_args(args, argc, argv);
    afs_assert(!ret, args_err, "unable to parse arguments");

    // Acquire the block device based on the args. This gives us a
    // wrapper on top of the kernel block device structure.
    ret = dm_get_device(ti, args->passive_dev, dm_table_get_mode(ti->table), &context->passive_dev);
    afs_assert(!ret, args_err, "could not find given disk [%s]", args->passive_dev);
    context->bdev = context->passive_dev->bdev;
    context->config.bdev_size = context->bdev->bd_part->nr_sects;

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

    // We are now ready to process map requests.
    context->ground_wq = alloc_workqueue("%s", WQ_UNBOUND | WQ_HIGHPRI, 1, "Artifice Ground WQ");
    afs_action(!IS_ERR(context->ground_wq), ret = PTR_ERR(context->ground_wq), gwq_err, "could not create gwq [%d]", ret);

    //maybe remove the WQ_CPU_INTENSIVE option
    context->flight_wq = alloc_workqueue("%s", WQ_UNBOUND | WQ_HIGHPRI | WQ_CPU_INTENSIVE, num_online_cpus(), "Artifice Flight WQ");
    afs_action(!IS_ERR(context->flight_wq), ret = PTR_ERR(context->flight_wq), fwq_err, "could not create fwq [%d]", ret);

    INIT_WORK(&context->ground_ws, afs_groundq);
    INIT_WORK(&context->clean_ws, afs_cleanq);

    afs_eq_init(&context->ground_eq);
    afs_eq_init(&context->flight_eq);

    //TODO, change from the default number of blocks
    ret = cauchy_init();
    afs_assert(!ret, encode_err, "could not initialize encoding library [%d]", ret);
//    context->params.BlockBytes = AFS_BLOCK_SIZE;
//    context->params.OriginalCount = context->config.num_carrier_blocks + context->config.num_entropy_blocks;
//    context->params.RecoveryCount = context->params.OriginalCount;

    afs_debug("constructor completed");
    ti->private = context;
    return 0;

fwq_err:
    destroy_workqueue(context->ground_wq);

gwq_err:
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

encode_err:
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
    while (!afs_eq_empty(&context->flight_eq) || !afs_eq_empty(&context->ground_eq)) {
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
    destroy_workqueue(context->ground_wq);

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
