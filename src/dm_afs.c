/*
 * Author: Yash Gupta <ygupta@ucsc.edu>, Austen Barker <atbarker@ucsc.edu>
 * Copyright: UC Santa Cruz, SSRC
 */
#include <dm_afs.h>
#include <dm_afs_utilities.h>
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
 * @return  FS_XXXX/ FS_ERR.
 */
static int8_t 
detect_fs(struct block_device *device, struct afs_private *context)
{
    const sector_t start_sector = 0;
    const uint32_t read_length = PAGE_SIZE;

    struct page *page = NULL;
    void *page_addr = NULL;
    struct afs_passive_fs *fs = NULL;
    int8_t ret = FS_ERR;

    // Build the IO request to read in a page.
    struct afs_io read_request = {
        .bdev = device,
        .io_sector = start_sector,
        .io_size = read_length,
        .type = IO_READ
    };

    // Allocate a page in memory and acquire a virtual address
    // to it.
    page = alloc_page(GFP_KERNEL);
    afs_assert_action(!IS_ERR(page), ret = PTR_ERR(page), alloc_err, "could not allocate page [%d]", ret);
    page_addr = page_address(page);

    read_request.io_page = page;
    ret = afs_blkdev_io(&read_request);
    afs_assert(!ret, read_err, "could not read page [%d]", ret);
 
    fs = kmalloc(sizeof(*fs), GFP_KERNEL);
    afs_assert_action(!IS_ERR(fs), ret = PTR_ERR(fs), fs_alloc_err, "could not allocate passive FS [%d]", ret);

    // Add more detection functions as a series of else..if blocks.
    if (afs_fat32_detect(page_addr, device, fs)) {
        ret = FS_FAT32;
    } else if (afs_ext4_detect(page_addr, device, fs)) {
        ret = FS_EXT4;
    } else if (afs_ntfs_detect(page_addr, device, fs)) {
        ret = FS_NTFS;
    } else {
        ret = FS_ERR;
    }
    context->passive_fs = fs;
    __free_page(page);

    afs_debug("detected %d", ret);
    return ret;

fs_alloc_err:
    // Nothing to do.

read_err:
    __free_page(page);

alloc_err:
    return FS_ERR;
}

/**
 * Parse the supplied arguments from the user.
 */
static int32_t
parse_afs_args(struct afs_args *args, unsigned int argc, char *argv[])
{
    const int8_t TYPE = 0;
    const int8_t PASSPHRASE = 1;
    const int8_t DISK = 2;
    int i;

    afs_assert(argc >= 3, err, "not enough arguments");
    memset(args, 0, sizeof(*args));

    // These three are always required.
    afs_assert(!kstrtou8(argv[TYPE], 10, &args->instance_type), err, "incorrect instance type");
    memcpy(args->passphrase, argv[PASSPHRASE], PASSPHRASE_SZ);
    memcpy(args->passive_dev, argv[DISK], PASSIVE_DEV_SZ);
    afs_debug("%d | %s | %s", args->instance_type, args->passphrase, args->passive_dev);

    // These may be optional depending on the type.
    for (i = DISK + 1; i < argc; i++) {
        if (!strcmp(argv[i], "--entropy")) {
            afs_assert(++i < argc, err, "missing value [entropy source]");
            memcpy(args->entropy_dir, argv[i], ENTROPY_DIR_SZ);
        } else if (!strcmp(argv[i], "--shadow_passphrase")) {
            afs_assert(++i < argc, err, "missing value [shadow passphrase]");
            memcpy(args->shadow_passphrase, argv[i], PASSPHRASE_SZ);
        } else {
            afs_assert(0, err, "unknown argument");
        }
    }
    afs_debug("%s | %s", args->entropy_dir, args->shadow_passphrase);

    // Now that we have all the arguments, we need to make sure
    // that they semantically make sense.
    switch (args->instance_type) {
        case TYPE_NEW:
            afs_assert(args->entropy_dir[0] != 0, err, "entropy source not provided");
            afs_assert(args->shadow_passphrase[0] == 0, err, "shadow passphrase provided");
            break;

        case TYPE_ACCESS:
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
 * Constructor function for this target. The constructor
 * is called for each new instance of a device for this
 * target. To create a new device, 'dmsetup create' is used.
 * 
 * The constructor is used to parse the program arguments
 * and detect the file system in effect on the passive
 * block device.
 * 
 * @ti      Target instance for new device.
 * @return  0       New device instance successfully created.
 * @return  <0      Error.
 */
static int
afs_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
    int map_offset;
    unsigned char *digest = NULL;
    struct afs_super *super = NULL;

    struct afs_private *context = NULL;
    struct afs_args *args = NULL;
    int ret;
    int8_t fs;

    context = kmalloc(sizeof *context, GFP_KERNEL);
    afs_assert_action(!IS_ERR(context), ret = PTR_ERR(context), context_err, "kmalloc failure [%d]", ret);

    args = &context->instance_args;
    ret = parse_afs_args(args, argc, argv);
    afs_assert(!ret, args_err, "unable to parse arguments");

    // Acquire the block device based on the args. This gives us a 
    // wrapper on top of the kernel block device structure.
    ret = dm_get_device(ti, args->passive_dev, dm_table_get_mode(ti->table), &context->passive_dev);
    afs_assert(!ret, args_err, "could not find given disk [%s]", args->passive_dev);

    fs = detect_fs(context->passive_dev->bdev, context);
    switch (fs) {
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
            afs_assert_action(0, ret = -ENOENT, fs_err, "unknown file system");
            break;
        
        default:
            afs_assert_action(0, ret = -ENXIO, fs_err, "seems like all hell broke loose");
    }

//     context->fs_context->allocation = kmalloc((context->fs_context->list_len), GFP_KERNEL);

//     //Generate the hash of our password.
//     digest = kmalloc(sizeof(DM_MKS_PASSPHRASE_SZ), GFP_KERNEL);
//     ret = passphrase_hash((unsigned char *)context->passphrase, (unsigned int)DM_MKS_PASSPHRASE_SZ, digest);

//     //generate superblock locations

//     //write the superblock copies to the disk or search for the superblock
//     //if(1){
//     //if(argc == DM_MKS_ARG_MAX){
// 	    map_offset = random_offset(1000);
// 	    //Generate the superblock
//         super = generate_superblock(digest, ti->len / context->fs_context->sectors_per_block, 0, 0, map_offset);
	
// 	    //Write the superblock
//         write_new_superblock(super, 1, digest, context->fs_context, context->passive_dev->bdev);

//         afs_debug("Superblock written\n");
	
// 	    //write the new matryoshka map
//         context->map = write_new_map(ti->len / context->fs_context->sectors_per_block, context->fs_context, context->passive_dev->bdev, map_offset);
	
//         afs_debug("Artifice Formatting Complete.\n");
//     //}else{
// 	    //retrieve the superblock
//         super = retrieve_superblock(1, digest, context->fs_context, context->passive_dev->bdev);
	
// 	    //Perform an integrity check on where the superblocks are
//         if(super == NULL){
// 		    afs_alert("Could not find superblock with passphrase\n");
// 		    return -1;
// 	    }else{
//             afs_debug("Found superblock\n");
// 	    }
//         //context->map = retrieve_map((u32)super->afs_size, context->fs_context, context->passive_dev->bdev, super);
//    // }
//     afs_debug("length of artifice %lu", ti->len);
//     afs_debug("sectors per block %d", context->fs_context->sectors_per_block);

//     afs_info("exiting constructor\n");


    afs_debug("constructor completed");
    ti->private = context;
    
    return 0;

fs_err:
    dm_put_device(ti, context->passive_dev);

args_err:
    kfree(context);

context_err:
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

    dm_put_device(ti, context->passive_dev);
    kfree(context);
    afs_debug("destructor completed\n");
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
    struct afs_private *context;

    context = ti->private;
    switch(bio_op(bio)) {
        case REQ_OP_READ:
            afs_debug("read operation");
            break;

        case REQ_OP_WRITE:
            afs_debug("write operation");
            break;

        default:
            afs_debug("unknown operation");
    }
    
    // Each bio needs to be handled somehow, otherwise the kernel thread
    // belonging to it freezes. Even shutdown won't work as a kernel thread is
    // engaged.
    bio_endio(bio);
    
    return DM_MAPIO_SUBMITTED;
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
    afs_debug("unregistered dm_afs\n");
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
