/*
 * Author: Yash Gupta <ygupta@ucsc.edu>, Austen Barker <atbarker@ucsc.edu>
 * Copyright: UC Santa Cruz, SSRC
 */
#include <dm_afs.h>
#include <dm_afs_modules.h>

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
    afs_assert_action(page, ret = -ENOMEM, alloc_err, "could not allocate page [%d]", ret);
    ret = read_page(page, device, 0, false);
    afs_assert(!ret, read_err, "could not read page [%d]", ret);

    // Add more detection functions as a series of else..if blocks.
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
    strncpy(args->passphrase, argv[PASSPHRASE], PASSPHRASE_SZ-1);
    strncpy(args->passive_dev, argv[DISK], PASSIVE_DEV_SZ-1);
    afs_debug("Type: %d", args->instance_type);
    afs_debug("Passphrase: %s", args->passphrase);
    afs_debug("Device: %s", args->passive_dev);

    // These may be optional depending on the type.
    // TODO: Add long parameter for reed-solomon configuration.
    for (i = DISK + 1; i < argc; i++) {
        if (!strcmp(argv[i], "--entropy")) {
            afs_assert(++i < argc, err, "missing value [entropy source]");
            strncpy(args->entropy_dir, argv[i], ENTROPY_DIR_SZ-1);
        } else if (!strcmp(argv[i], "--shadow_passphrase")) {
            afs_assert(++i < argc, err, "missing value [shadow passphrase]");
            strncpy(args->shadow_passphrase, argv[i], PASSPHRASE_SZ-1);
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
 * Callback scheduled thread for the map function.
 */
static void
__work_afs_map(struct work_struct *work)
{
    struct afs_private *context;
    int ret;

    context = container_of(work, struct afs_private, map_work);
    switch(bio_op(context->bio)) {
        case REQ_OP_READ:
            ret = afs_read_request(context, context->bio);
            break;

        case REQ_OP_WRITE:
            ret = afs_write_request(context, context->bio);
            break;

        case REQ_OP_FLUSH:
            // We are anyway writing everything to disk directly,
            // so this is like a nop.
            ret = 0;
            break;

        default:
            // This case should never be encountered.
            ret = -EINVAL;
            afs_debug("What the hell!");
    }
    afs_assert(!ret, done, "could not perform operation [%d:%d]", ret, bio_op(context->bio));

done:
    bio_endio(context->bio);
    context->bio = NULL;
    wake_up_interruptible(&context->bio_waitq);
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
    int i, ret;
    int8_t detected_fs;
    uint64_t instance_size;

    // Make sure instance is large enough.
    instance_size = ti->len * AFS_SECTOR_SIZE;
    afs_assert_action(instance_size >= AFS_MIN_SIZE, ret = -EINVAL, err, "instance too small [%llu]", instance_size);

    // Confirm our structure sizes.
    afs_assert_action(sizeof(*sb) == AFS_BLOCK_SIZE, ret = -EINVAL, err, "super block structure incorrect size [%lu]", sizeof(*sb));
    afs_assert_action(sizeof(struct afs_ptr_block) == AFS_BLOCK_SIZE, ret = -EINVAL, err, "pointer block structure incorrect size");

    context = kmalloc(sizeof(*context), GFP_KERNEL);
    afs_assert_action(context, ret = -ENOMEM, err, "kmalloc failure [%d]", ret);
    memset(context, 0, sizeof(*context));
    context->instance_size = instance_size;
    context->current_process = current;

    // Initialize the work queues.
    context->map_queue = create_singlethread_workqueue("afs_map queue");
    afs_assert_action(!IS_ERR(context->map_queue), ret = PTR_ERR(context->map_queue), wq_err, "could not create wq [%d]", ret);
    INIT_WORK(&context->map_work, __work_afs_map);
    init_waitqueue_head(&context->bio_waitq);

    args = &context->instance_args;
    ret = parse_afs_args(args, argc, argv);
    afs_assert(!ret, args_err, "unable to parse arguments");

    // Acquire the block device based on the args. This gives us a 
    // wrapper on top of the kernel block device structure.
    ret = dm_get_device(ti, args->passive_dev, dm_table_get_mode(ti->table), &context->passive_dev);
    afs_assert(!ret, args_err, "could not find given disk [%s]", args->passive_dev);
    context->bdev = context->passive_dev->bdev;

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
            // afs_assert_action(0, ret = -ENOENT, fs_err, "unknown file system");
            break;
        
        default:
            afs_assert_action(0, ret = -ENXIO, fs_err, "seems like all hell broke loose");
    }

    // This is all just for testing.
    // BEGIN.
    fs->block_list = kmalloc(65536 * sizeof(*fs->block_list), GFP_KERNEL);
    fs->list_len = 65536;

    for (i = 0; i < fs->list_len; i++) {
        fs->block_list[i] = i;
    }
    // END.

    // Allocate the free list allocation vector to be able
    // to map all possible blocks and mask the invalid block.
    context->allocation_vec = bit_vector_create(((uint64_t)1 << 32) - 1);
    afs_assert_action(context->allocation_vec, ret = -ENOMEM, vec_err, "could not allocate allocation vector");
    spin_lock_init(&context->allocation_lock);
    allocation_set(context, AFS_INVALID_BLOCK);

    sb = &context->super_block;
    switch (args->instance_type) {
        case TYPE_CREATE:
            // TODO: Acquire carrier block count from RS parameters.
            build_configuration(context, 4);
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
    
    afs_debug("constructor completed");
    ti->private = context;
    return 0;

sb_err:
    bit_vector_free(context->allocation_vec);

vec_err:
    kfree(fs->block_list);

fs_err:
    dm_put_device(ti, context->passive_dev);

args_err:
    destroy_workqueue(context->map_queue);

wq_err:
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
    bit_vector_free(context->allocation_vec);

    // Free the block list for the passive FS.
    kfree(context->passive_fs.block_list);

    // Put the device back.
    dm_put_device(ti, context->passive_dev);
    
    // Destroy the map queue.
    destroy_workqueue(context->map_queue);

    // Free storage used by context.
    kfree(context);
    afs_debug("destructor completed");
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
    struct afs_private *context = ti->private;
    uint32_t sector_offset;
    uint32_t max_sector_count;
    int ret;

    do {
        ret = wait_event_interruptible(context->bio_waitq, context->bio == NULL);
    } while (ret == -ERESTARTSYS);
    context->bio = bio;

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

    switch(bio_op(bio)) {
        case REQ_OP_READ:
        case REQ_OP_WRITE:
        case REQ_OP_FLUSH:
            queue_work(context->map_queue, &context->map_work);
            return DM_MAPIO_SUBMITTED;
            break;

        default:
            afs_debug("unknown operation");
            context->bio = NULL;
            bio_endio(bio);
            return DM_MAPIO_KILL;
    }
}

/** ----------------------------------------------------------- DO-NOT-CROSS ------------------------------------------------------------------- **/

static struct target_type afs_target = {
    .name = DM_AFS_NAME,
    .version = {DM_AFS_MAJOR_VER, DM_AFS_MINOR_VER, DM_AFS_PATCH_VER},
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
