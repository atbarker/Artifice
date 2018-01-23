/**
 * Target file source for the matryoshka file system.
 * 
 * Author: 
 * Copyright: UC Santa Cruz, SSRC
 * 
 * License: 
 */
#include <dm_mks.h>

// Data structure used by the device mapper framework 
// for registering our specific callbacks.
static struct target_type dm_mks_target = {
    .name = DM_MKS_NAME,
    .version = {DM_MKS_MAJOR_VER, DM_MKS_MINOR_VER, DM_MKS_PATCH_VER},
    .module = THIS_MODULE,
    .ctr = dm_mks_ctr,
    .dtr = dm_mks_dtr,
    .map = dm_mks_map
};

/**
 * Constructor function for this target. The constructor
 * is called for each new instance of a device for this
 * target. To create a new device, 'dmsetup create' is used.
 * 
 * The function allocates required structures and sets the
 * private context variable for the target instance.
 * 
 * @param   ti      Target instance for new device.
 * @param   argc    Argument count passed while creating device.
 * @param   argv    Argument values as strings.
 * 
 * @return  0       New device instance successfully created.
 * @return  <0      Error.
 *  -EINVAL:        Not enough arguments.
 *  -ERROR:         ...
 */
static int
dm_mks_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
    struct dm_mks_private *this_instance = NULL;

    dm_mks_info("entering constructor\n");
    dm_mks_debug("arg count: %d\n", argc);
    if (argc != DM_MKS_ARG_MAX) {
        dm_mks_alert("not enough arguments\n");
        return -EINVAL;
    }

    this_instance = kmalloc(sizeof *this_instance, GFP_KERNEL);
    if (!this_instance) {
        dm_mks_alert("kmalloc failure\n");
    }
    dm_mks_debug("this_instance: %p\n", this_instance);

    // TODO: Not sure if the argv pointers survive beyond constructor.
    this_instance->passphrase = argv[DM_MKS_ARG_PASSPHRASE];
    this_instance->phys_block_dev = argv[DM_MKS_ARG_BLOCKDEV];
    ti->private = this_instance;

    dm_mks_info("exiting constructor\n");
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
dm_mks_dtr(struct dm_target *ti)
{
    struct dm_mks_private *this_instance = ti->private;

    dm_mks_info("entering destructor\n");
    kfree(this_instance);
    dm_mks_info("exiting destructor\n");
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
dm_mks_map(struct dm_target *ti, struct bio *bio)
{   
    dm_mks_debug("entering mapper\n");

    /*
     * TODO: Each bio needs to be handled somehow, otherwise the kernel thread
     * belonging to it freezes. Even shutdown wont work as a kernel thread is
     * engaged.
     */ 
    bio_endio(bio);
    
    dm_mks_debug("exiting mapper\n");
    return DM_MAPIO_SUBMITTED;
}

/**
 * Initialization function called when the module
 * is inserted dynamically into the kernel. It registers
 * the dm_mks target into the device-mapper tree.
 * 
 * @return  0   Target registered, no errors.
 * @return  <0  Target registration failed.
 */
static __init int 
dm_mks_init(void)
{
    int ret;

    ret = dm_register_target(&dm_mks_target);
    if (ret < 0) {
        dm_mks_alert("Registration failed: %d\n", ret);
    }
    dm_mks_debug("Registered dm_mks\n");

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
dm_mks_exit(void)
{
    dm_unregister_target(&dm_mks_target);
    dm_mks_debug("Unregistered dm_mks\n");
}

// Module Init
module_init(dm_mks_init);
module_exit(dm_mks_exit);

// Module description
MODULE_AUTHOR("Austen Barker, Yash Gupta");
MODULE_LICENSE("GPL");

// Module parameters.
module_param(dm_mks_debug_mode, int, 0644);
MODULE_PARM_DESC(dm_mks_debug_mode, "Set to 1 to enable debug mode {affects performance}");