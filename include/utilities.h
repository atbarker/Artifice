/**
 * Basic utility system for miscellaneous functions.
 * 
 * Author: Yash Gupta <ygupta@ucsc.edu>
 * Copyright: UC Santa Cruz, SSRC
 */
#include <linux/stddef.h>

//
// Macros
//
// Printing and debugging.
#define dm_mks_info(fmt, ...)                                   \
    do {                                                        \
        printk(KERN_INFO "dm-mks-info: " fmt, ##__VA_ARGS__);   \
    } while (0)

#define dm_mks_debug(fmt, ...)                                  \
    do {                                                        \
        if (dm_mks_debug_mode) {                                \
            printk(KERN_DEBUG "dm-mks-debug: [%s:%d] " fmt,     \
            __func__, __LINE__,                                 \
            ##__VA_ARGS__);                                     \
        }                                                       \
    } while (0)

#define dm_mks_alert(fmt, ...)                              \
    do {                                                    \
        printk(KERN_ALERT "dm-mks-alert: [%s:%d] " fmt,     \
        __func__, __LINE__,                                 \
        ##__VA_ARGS__);                                     \
    } while (0)

/**
 * Perform a simple hexdump of data.
 * TODO: printk creates a new line. Needs buffering.
 * 
 * @param   data    Data to be examined.
 * @param   length  Length of the data.
 */
static inline void
hex_dump(u8 *data, u32 length)
{
    u32 i;

    dm_mks_debug("performing hexdump: %p\n", data);
    for (i = 0; i < length; i++) {
        printk(KERN_INFO "%2X ", data[i]);
    }
}