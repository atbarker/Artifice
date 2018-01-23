/**
 * Target file source for the matryoshka file system.
 * 
 * Author: Yash Gupta <ygupta@ucsc.edu>, 
 * Copyright: UC Santa Cruz, SSRC
 */
#include <dm_mks.h>
#include <utilities.h>

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
    struct dm_mks_private *context = NULL;

    dm_mks_info("entering constructor\n");
    dm_mks_debug("arg count: %d\n", argc);
    if (argc != DM_MKS_ARG_MAX) {
        dm_mks_alert("not enough arguments\n");
        return -EINVAL;
    }

    context = kmalloc(sizeof *context, GFP_KERNEL);
    if (!context) {
        dm_mks_alert("kmalloc failure\n");
    }
    dm_mks_debug("context: %p\n", context);

    strcpy(context->passphrase, argv[DM_MKS_ARG_PASSPHRASE]);
    strcpy(context->passive_dev_name, argv[DM_MKS_ARG_PASSIVE_DEV]);

    ti->private = context;
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
    struct dm_mks_private *context = ti->private;

    dm_mks_info("entering destructor\n");
    kfree(context);
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
    struct dm_mks_private *context = ti->private;
    void *data;

    dm_mks_debug("entering mapper\n");
    switch(bio_op(bio)) {
        case REQ_OP_READ:
            dm_mks_debug("read op\n");
            break;
        case REQ_OP_WRITE:
            dm_mks_debug("write op\n");
            break;
        default:
            dm_mks_debug("unknown op\n");
    }

    data = bio_data(bio);
    hex_dump(data, bio_cur_bytes(bio));

    /*
     * TODO: Each bio needs to be handled somehow, otherwise the kernel thread
     * belonging to it freezes. Even shutdown wont work as a kernel thread is
     * engaged.
     */ 
    bio_endio(bio);
    
    dm_mks_debug("exiting mapper\n");
    return DM_MAPIO_SUBMITTED;
}

static struct target_type dm_mks_target = {
    .name = DM_MKS_NAME,
    .version = {DM_MKS_MAJOR_VER, DM_MKS_MINOR_VER, DM_MKS_PATCH_VER},
    .module = THIS_MODULE,
    .ctr = dm_mks_ctr,
    .dtr = dm_mks_dtr,
    .map = dm_mks_map
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

module_init(dm_mks_init);
module_exit(dm_mks_exit);
MODULE_AUTHOR("Austen Barker, Yash Gupta");
MODULE_LICENSE("GPL");

//
// Module Parameters
//
// dm_mks_debug_mode
module_param(dm_mks_debug_mode, int, 0644);
MODULE_PARM_DESC(dm_mks_debug_mode, "Set to 1 to enable debug mode {affects performance}");