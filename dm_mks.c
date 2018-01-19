/**
 * Target file source for the matryoshka file system.
 * 
 * Author: 
 * Copyright: UC Santa Cruz, SSRC
 * 
 * License: 
 */
#include "dm_mks.h"

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
 *  -EINVAL:        ...
 */
static int
dm_mks_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
    dm_mks_info("In Constructor!\n");
    return 0;
}

/**
 * Destructor function for this target. The destructor
 * is calle when a device instance for this target is
 * destroyed. It frees up the space used up for this 
 * instance.
 * 
 * @param   ti      Target instance to be destroyed.
 */ 
static void 
dm_mks_dtr(struct dm_target *ti)
{
    dm_mks_info("In Destructor!\n");
}

/**
 * Map function for this target. This is the heart and soul
 * of the device mapper. We receive block I/O requests which
 * we need to remap to our underlying device and then submit
 * the request. 
 * 
 * This function is essentially called for any I/O on a device
 * for this target.
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
    dm_mks_info("In Map!\n");
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
static int 
dm_mks_init(void)
{
    int ret;

    ret = dm_register_target(&dm_mks_target);
    if (ret < 0) {
        dm_mks_alert("Registration failed: %d\n", ret);
    } else {
        dm_mks_debug("Registered dm_mks\n");
    }

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
}

// Module Init
module_init(dm_mks_init);
module_exit(dm_mks_exit);

// Module description
// MODULE_AUTHOR("");

// Module parameters.
module_param(debug_enable, int, 0644);
MODULE_PARM_DESC(debug_enable, "Set to 1 to enable debugging");