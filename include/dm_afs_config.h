/**
 * Author: Yash Gupta <ygupta@ucsc.edu>, Austen Barker <atbarker@ucsc.edu>
 * Copyright: UC Santa Cruz, SSRC
 */
#include <linux/kernel.h>

#ifndef DM_AFS_CONFIG_H
#define DM_AFS_CONFIG_H

// Standard integer types.
typedef s8 int8_t;
typedef u8 uint8_t;
typedef s16 int16_t;
typedef u16 uint16_t;
typedef s32 int32_t;
typedef u32 uint32_t;
typedef s64 int64_t;
typedef u64 uint64_t;

// Metadata
#define DM_AFS_NAME "artifice"
#define DM_AFS_MAJOR_VER 0
#define DM_AFS_MINOR_VER 1
#define DM_AFS_PATCH_VER 0

// Simple information.
#define afs_info(fmt, ...)                                         \
    ({                                                             \
        printk(KERN_INFO "dm-afs-info: " fmt "\n", ##__VA_ARGS__); \
    })

// Debug information.
#define afs_debug(fmt, ...)                                      \
    ({                                                           \
        if (afs_debug_mode) {                                    \
            printk(KERN_DEBUG "dm-afs-debug: [%s:%d] " fmt "\n", \
                __func__, __LINE__,                              \
                ##__VA_ARGS__);                                  \
        }                                                        \
    })

// Alert information.
#define afs_alert(fmt, ...)                                  \
    ({                                                       \
        printk(KERN_ALERT "dm-afs-alert: [%s:%d] " fmt "\n", \
            __func__, __LINE__,                              \
            ##__VA_ARGS__);                                  \
    })

// Assert and jump.
#define afs_assert(cond, label, fmt, args...) \
    ({                                        \
        if (!(cond)) {                        \
            afs_alert(fmt, ##args);           \
            goto label;                       \
        }                                     \
    })

// Assert, perform an action, and jump.
#define afs_action(cond, action, label, fmt, args...) \
    ({                                                \
        if (!(cond)) {                                \
            action;                                   \
            afs_alert(fmt, ##args);                   \
            goto label;                               \
        }                                             \
    })

/**
 * Constants.
 */
enum {
    // Configuration.
    AFS_MIN_SIZE = 1 << 16,
    AFS_BLOCK_SIZE = 4096,
    AFS_SECTOR_SIZE = 512,
    AFS_SECTORS_PER_BLOCK = 8,
    AFS_INVALID_BLOCK = U32_MAX,
    NUM_MAP_BLKS_IN_SB = 975,
    NUM_MAP_BLKS_IN_PB = 1019,
    NUM_DEFAULT_CARRIER_BLKS = 4,
    NUM_MAX_CARRIER_BLKS = 8,
    NUM_SUPERBLOCK_REPLICAS = 8,

    // Array sizes.
    PASSPHRASE_SZ = 64,
    PASSIVE_DEV_SZ = 32,
    ENTROPY_DIR_SZ = 64,
    ENTROPY_HASH_SZ = 8,
    CARRIER_HASH_SZ = 32,

    // Hash algorithms
    SHA1_SZ = 20,
    SHA128_SZ = 16,
    SHA256_SZ = 32,
    SHA512_SZ = 64,

    // Artifice type.
    TYPE_CREATE = 0,
    TYPE_MOUNT = 1,
    TYPE_SHADOW = 2,

    // File system support.
    FS_FAT32 = 0,
    FS_EXT4 = 1,
    FS_NTFS = 2,
    FS_ERR = -1
};

#endif /* DM_AFS_CONFIG_H */
