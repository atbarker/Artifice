/**
 * Target file source for the matryoshka file system.
 * 
 * Author: Yash Gupta <ygupta@ucsc.edu>, 
 * Copyright: UC Santa Cruz, SSRC
 */
#include <dm_mks.h>
#include <dm_mks_utilities.h>
#include <dm_mks_lib.h>

/**
 * Constructor function for this target. The constructor
 * is called for each new instance of a device for this
 * target. To create a new device, 'dmsetup create' is used.
 * 
 * The constructor is used to parse the program arguments
 * and detect the file system in effect on the passive
 * block device.
 * 
 * TODO: This function returns without clean up. Needs
 * goto's.
 * 
 * @param   ti      Target instance for new device.
 * @param   argc    Argument count passed while creating device.
 * @param   argv    Argument values as strings.
 * 
 * @return  0       New device instance successfully created.
 * @return  <0      Error.
 *  -EINVAL:        Not enough arguments.
 *  -EIO:           Could not open passive device.
 *  -ERROR:         ...
 */
static int
mks_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
    int ret;
    struct mks_private *context = NULL;

    mks_info("entering constructor\n");
    mks_debug("arg count: %d\n", argc);
    if (argc != DM_MKS_ARG_MAX) {
        mks_alert("not enough arguments\n");
        return -EINVAL;
    }

    context = kmalloc(sizeof *context, GFP_KERNEL);
    if (IS_ERR(context)) {
        ret = PTR_ERR(context);
        mks_alert("kmalloc failure {%d}\n", ret);
        return ret;
    }
    mks_debug("context: %p\n", context);
    ti->private = context;

    strncpy(context->passphrase, argv[DM_MKS_ARG_PASSPHRASE], DM_MKS_PASSPHRASE_SZ);
    strncpy(context->passive_dev_name, argv[DM_MKS_ARG_PASSIVE_DEV], DM_MKS_PASSIVE_DEV_SZ);

    ret = dm_get_device(ti, context->passive_dev_name, dm_table_get_mode(ti->table), &context->passive_dev);
    if (ret) {
        mks_alert("dm_get_device failure {%d}\n", ret);
        return ret;
    }

    ret = mks_detect_fs(context->passive_dev->bdev);
    if (ret < 0) {
        mks_alert("mks_detect_fs failure {%d}\n", ret);
        return ret;
    }
    switch (ret) {
        case DM_MKS_FS_FAT32:
            mks_debug("detected FAT32\n");
            break;
        case DM_MKS_FS_NONE:
            mks_debug("detected nothing\n");
            break;
    }

    mks_info("exiting constructor\n");
    return 0;
}

/**
 * Destructor function for this target. The destructor
 * is called when a device instance for this target is
 * destroyed. It frees up the space used up for this 
 * instance.
 * 
 * @param   ti      Target instance to be destroyed.
 */ 
static void 
mks_dtr(struct dm_target *ti)
{
    struct mks_private *context = ti->private;

    mks_info("entering destructor\n");
    kfree(context);
    mks_info("exiting destructor\n");
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
 * @param   ti      Target instance for the device.
 * @param   bio     The block I/O request to be processed.
 * 
 * @return  device-mapper code
 *  DM_MAPIO_SUBMITTED: dm_mks has submitted the bio request.
 *  DM_MAPIO_REMAPPED:  dm_mks has remapped the request and device-mapper
 *                      needs to submit it.
 *  DM_MAPIO_REQUEUE:   dm_mks encountered a problem and the bio needs to
 *                      be resubmitted.
 */
static int
mks_map(struct dm_target *ti, struct bio *bio)
{   
    struct mks_private *context = ti->private;

    __mks_set_debug(DM_MKS_DEBUG_DISABLE);
    mks_debug("entering mapper\n");
    switch(bio_op(bio)) {
        case REQ_OP_READ:
            mks_debug("read op\n");
            break;
        case REQ_OP_WRITE:
            mks_debug("write op\n");
            break;
        default:
            mks_debug("unknown op\n");
    }

    /*
     * TODO: Each bio needs to be handled somehow, otherwise the kernel thread
     * belonging to it freezes. Even shutdown won't work as a kernel thread is
     * engaged.
     */ 
    bio_endio(bio);
    
    mks_debug("exiting mapper\n");
    __mks_set_debug(DM_MKS_DEBUG_ENABLE);

    return DM_MAPIO_SUBMITTED;
}

/**
 * A procedure to detect the existing file system on a block
 * device or a block device partition.
 * 
 * This prodecure taps into library functions provided by
 * supported filesystems to determine if it is supported.
 * 
 * @param   device      Block device to look at.
 * 
 * @return  enum
 *  DM_MKS_FS_FAT32     Block Device is formatted as FAT32
 *  DM_MKS_FS_EXT       Block Device is formatted as EXT(should be ext2 for now)
 *  DM_MKS_FS_NTFS      Block Device is formatted as NTFS
 *  DM_MKS_FS_NONE      Block Device does not have a supported filesystem.
 */
static int 
mks_detect_fs(struct block_device *device)
{
    const sector_t start_sector = 0;
    const u32 read_length = 1 << PAGE_SHIFT;

    struct page *page;
    void *data;
    int ret;
    struct fs_data *fs = NULL;

    page = alloc_page(GFP_KERNEL);
    if (IS_ERR(page)) {
        ret = PTR_ERR(page);
        mks_alert("alloc_page failure {%d}\n", ret);
        return ret;
    }
    data = page_address(page);

    ret = mks_read_blkdev(device, page, start_sector, read_length);
    if (ret) {
        mks_alert("mks_read_blkdev failure {%d}\n", ret);
        return ret;
    }
    //print_hex_dump(KERN_INFO, "", DUMP_PREFIX_NONE, 5, 16, data, read_length, 1);

    //TODO: fix null pointer error with FAT32
    /* Add filesystem support here as more else...if blocks */
    if (mks_fat32_detect(data, fs, device) == DM_MKS_TRUE) {
        ret = DM_MKS_FS_FAT32;
	//mks_debug("number of blocks: %u\n", fs->num_blocks);
    } else {
        ret = DM_MKS_FS_NONE;
    }
    __free_page(page);

    mks_debug("returning from mks_detect_fs {%d}\n", ret);
    return ret;
}

/** ----------------------------------------------------------- DO-NOT-CROSS ------------------------------------------------------------------- **/

static struct target_type mks_target = {
    .name = DM_MKS_NAME,
    .version = {DM_MKS_MAJOR_VER, DM_MKS_MINOR_VER, DM_MKS_PATCH_VER},
    .module = THIS_MODULE,
    .ctr = mks_ctr,
    .dtr = mks_dtr,
    .map = mks_map
};

/**
 * Initialization function called when the module
 * is inserted dynamically into the kernel. It registers
 * the dm_mks target into the device-mapper tree.
 * 
 * @return  0   Target registered, no errors.
 * @return  <0  Target registration failed.
 */
static __init int 
mks_init(void)
{   
    int ret;

    ret = dm_register_target(&mks_target);
    if (ret < 0) {
        mks_alert("Registration failed: %d\n", ret);
    }
    mks_debug("Registered dm_mks\n");

    return ret;
}

/**
 * Destructor function called when module is removed
 * from the kernel. This function means nothing when the
 * module is statically linked into the kernel.
 * 
 * Unregisters the dm_mks target from the device-mapper
 * tree.
 */
static void
mks_exit(void)
{
    dm_unregister_target(&mks_target);
    mks_debug("Unregistered dm_mks\n");
}

module_init(mks_init);
module_exit(mks_exit);
MODULE_AUTHOR("Austen Barker, Yash Gupta");
MODULE_LICENSE("GPL");

//
// Module Parameters
//
// mks_debug_mode
module_param(mks_debug_mode, int, 0644);
MODULE_PARM_DESC(mks_debug_mode, "Set to 1 to enable debug mode {affects performance}");

/** -------------------------------------------------------------------------------------------------------------------------------------------- **/
