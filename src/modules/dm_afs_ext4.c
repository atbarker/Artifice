/*
 * Author: Eugene Chou
 * Copyright: UC Santa Cruz, SSRC
 */
#include <dm_afs.h>
#include <dm_afs_modules.h>
#include <lib/bit_vector.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/printk.h>

/**
 * Constants relative to the data blocks
 */
#define EXT4_GROUP0_PAD     1024
#define EXT4_NDIR_BLOCKS    12
#define EXT4_IND_BLOCK      EXT4_NDIR_BLOCKS
#define EXT4_DIND_BLOCK     (EXT4_IND_BLOCK + 1)
#define EXT4_TIND_BLOCK     (EXT4_DIND_BLOCK + 1)
#define EXT4_N_BLOCKS       (EXT4_TIND_BLOCK + 1)

static bool is_64bit = false;

/**
 * Combines lo and hi 32 bits into a single 64-bit integer.
 */
static inline uint64_t
lo_hi_64(uint32_t lo, uint32_t hi)
{
    return (uint64_t)((uint64_t)hi << 32) | lo;
}

/**
 * Combines lo and hi 16 bits into a single 32-bit integer.
 */
static inline uint32_t
lo_hi_32(uint16_t lo, uint16_t hi)
{
    return (uint32_t)((uint32_t)hi << 16) | lo;
}

/**
 * EXT4 superblock struct.
 */
struct ext4_superblock {
    __le32  s_inodes_count;         // Inodes count
    __le32  s_blocks_count_lo;      // Blocks count
    __le32  s_r_blocks_count_lo;    // Reserved blocks count
    __le32  s_free_blocks_count_lo; // Free blocks count
    __le32  s_free_inodes_count;    // Free inodes count
    __le32  s_first_data_block;     // First Data Block
    __le32  s_log_blk_sz;       // Block size
    __le32  s_log_cluster_size;     // Allocation cluster size
    __le32  s_blocks_per_group;     // # Blocks per group
    __le32  s_clusters_per_group;   // # Clusters per group
    __le32  s_inodes_per_group;     // # Inodes per group
    __le32  s_mtime;                // Mount time
    __le32  s_wtime;                // Write time
    __le16  s_mnt_count;            // Mount count
    __le16  s_max_mnt_count;        // Maximal mount count
    __le16  s_magic;                // Magic signature
    __le16  s_state;                // File system state
    __le16  s_errors;               // Behaviour when detecting errors
    __le16  s_minor_rev_level;      // Minor revision level
    __le32  s_lastcheck;            // Time of last check
    __le32  s_checkinterval;        // Max. time between checks
    __le32  s_creator_os;           // OS
    __le32  s_rev_level;            // Revision level
    __le16  s_def_resuid;           // Default uid for reserved blocks
    __le16  s_def_resgid;           // Default gid for reserved blocks

    // The following fields are for EXT4_DYNAMIC_REV superblocks only.
    __le32  s_first_ino;            // First non-reserved inode
    __le16  s_inode_size;           // Size of inode structure
    __le16  s_block_group_nr;       // Block group # of this superblock
    __le32  s_feature_compat;       // Compatible feature set
    __le32  s_feature_incompat;     // Incompatible feature set
    __le32  s_feature_ro_compat;    // Readonly-compatible feature set
    __u8    s_uuid[16];             // 128-bit uuid for volume
    char    s_volume_name[16];      // Volume name
    char    s_last_mounted[64];     // Directory where last mounted
    __le32  s_algorithm_usage_bitmap;   // For compression

    // Performance hints.
    __u8    s_prealloc_blocks;      // # of blocks to try to preallocate
    __u8    s_prealloc_dir_blocks;  // # to preallocate for dirs
    __le16  s_reserved_gdt_blocks;  // Per group desc for online growth

    // Journaling support valid if EXT4_FEATURE_COMPAT_HAS_JOURNAL set.
    __u8    s_journal_uuid[16];     // uuid of journal superblock */
    __le32  s_journal_inum;         // inode number of journal file */
    __le32  s_journal_dev;          // device number of journal file */
    __le32  s_last_orphan;          // start of list of inodes to delete */
    __le32  s_hash_seed[4];         // HTREE hash seed */
    __u8    s_def_hash_version;     // Default hash version to use */
    __u8    s_jnl_backup_type;
    __le16  s_desc_size;            // size of group descriptor */
    __le32  s_default_mount_opts;
    __le32  s_first_meta_bg;        // First metablock block group */
    __le32  s_mkfs_time;            // When the filesystem was created */
    __le32  s_jnl_blocks[17];       // Backup of the journal inode */

    // 64bit support valid if EXT4_FEATURE_COMPAT_64
    __le32  s_blocks_count_hi;      // Blocks count
    __le32  s_r_blocks_count_hi;    // Reserved blocks count
    __le32  s_free_blocks_count_hi; // Free blocks count
    __le16  s_min_extra_isize;      // All inodes have at least # bytes
    __le16  s_want_extra_isize;     // New inodes should reserve # bytes
    __le32  s_flags;                // Miscellaneous flags
    __le16  s_raid_stride;          // RAID stride
    __le16  s_mmp_update_interval;  // # seconds to wait in MMP checking
    __le64  s_mmp_block;            // Block for multi-mount protection
    __le32  s_raid_stripe_width;    // Blocks on all data disks (N*stride)
    __u8    s_log_groups_per_flex;  // FLEX_BG group size
    __u8    s_checksum_type;        // Metadata checksum algorithm used
    __u8    s_encryption_level;     // Versioning level for encryption
    __u8    s_reserved_pad;         // Padding to next 32bits
    __le64  s_kbytes_written;       // # of lifetime kilobytes written
    __le32  s_snapshot_inum;        // Inode number of active snapshot
    __le32  s_snapshot_id;          // Sequential ID of active snapshot
    __le64  s_snapshot_r_blocks_count; // Snapshot future use reserved blocks
    __le32  s_snapshot_list;        // Inode # of head of on-disk snapshot list

#define EXT4_S_ERR_START offsetof(struct ext4_superblock, s_error_count)
    __le32  s_error_count;          // Number of fs errors */
    __le32  s_first_error_time;     // First time an error happened */
    __le32  s_first_error_ino;      // Inode involved in first error */
    __le64  s_first_error_block;    // Block involved of first error */
    __u8    s_first_error_func[32]; // Function where error happened
    __le32  s_first_error_line;     // Line number where error happened */
    __le32  s_last_error_time;      // Most recent time of an error */
    __le32  s_last_error_ino;       // Inode involved in last error */
    __le32  s_last_error_line;      // Line number where error happened */
    __le64  s_last_error_block;     // Block involved of last error */
    __u8    s_last_error_func[32];  // Function where error happened

#define EXT4_S_ERR_END offsetof(struct ext4_superblock, s_mount_opts)
    __u8    s_mount_opts[64];
    __le32  s_usr_quota_inum;       // Inode for tracking user quota
    __le32  s_grp_quota_inum;       // Inode for tracking group quota
    __le32  s_overhead_clusters;    // Overhead blocks/clusters in fs
    __le32  s_backup_bgs[2];        // Groups with sparse_super2 SBs
    __u8    s_encrypt_algos[4];     // Encryption algorithms in use
    __u8    s_encrypt_pw_salt[16];  // Salt used for string2key algorithm
    __le32  s_lpf_ino;              // Location of the lost+found inode
    __le32  s_prj_quota_inum;       // Inode for tracking project quota
    __le32  s_checksum_seed;        // crc32c(uuid) if csum_seed set
    __u8    s_wtime_hi;
    __u8    s_mtime_hi;
    __u8    s_mkfs_time_hi;
    __u8    s_lastcheck_hi;
    __u8    s_first_error_time_hi;
    __u8    s_last_error_time_hi;
    __u8    s_pad[2];
    __le16  s_encoding;             // Filename charset encoding
    __le16  s_encoding_flags;       // Filename charset encoding flags
    __le32  s_reserved[95];         // Padding to the end of the block
    __le32  s_checksum;             // crc32c(superblock)
} __attribute__((packed));

/**
 * Enumeration of EXT4 OS creators.
 */
enum ext4_os_creators {
    LINUX   = 0,
    HURD    = 1,
    MASIX   = 2,
    FREEBSD = 3,
    LITES   = 4
};

/**
 * Enumeration of EXT4 compatible feature set flags.
 */
enum ext4_feat_compat_flags {
    EXT4_COMPAT_DIR_PREALLOC    = 0x1,
    EXT4_COMPAT_IMAGIC_INODES   = 0x2,
    EXT4_COMPAT_HAS_JOURNAL     = 0x4,
    EXT4_COMPAT_EXT_ATTR        = 0x8,
    EXT4_COMPAT_RESIZE_INODE    = 0x10,
    EXT4_COMPAT_DIR_INDEX       = 0x20,
    EXT4_COMPAT_LAZY_BG         = 0x40,
    EXT4_COMPAT_EXCLUDE_INODE   = 0x80,
    EXT4_COMPAT_EXCLUDE_BITMAP  = 0x100,
    EXT4_COMPAT_SPARSE_SUPER2   = 0x200
};

/**
 * Enumeration of EXT4 incompatible feature set flags.
 */
enum ext4_feat_incompat_flags {
    EXT4_INCOMPAT_COMPRESSION   = 0x1,
    EXT4_INCOMPAT_FILETYPE      = 0x2,
    EXT4_INCOMPAT_RECOVER       = 0x4,
    EXT4_INCOMPAT_JOURNAL_DEV   = 0x8,
    EXT4_INCOMPAT_META_BG       = 0x10,
    EXT4_INCOMPAT_EXTENTS       = 0x40,
    EXT4_INCOMPAT_64BIT         = 0x80,
    EXT4_INCOMPAT_MMP           = 0x100,
    EXT4_INCOMPAT_FLEX_BG       = 0x200,
    EXT4_INCOMPAT_EA_INODE      = 0x400,
    EXT4_INCOMPAT_DIRDATA       = 0x1000,
    EXT4_INCOMPAT_CSUM_SEED     = 0x2000,
    EXT4_INCOMPAT_LARGEDIR      = 0x4000,
    EXT4_INCOMPAT_INLINE_DATA   = 0x8000,
    EXT4_INCOMPAT_ENCRYPT       = 0x10000
};

/**
 * Enumeration of EXT4 read-only compatible feature set flags.
 */
enum ext4_feat_ro_compat_flags {
    EXT4_RO_COMPAT_SPARSE_SUPER     = 0x1,
    EXT4_RO_COMPAT_LARGE_FILE       = 0x2,
    EXT4_RO_COMPAT_BTREE_DIR        = 0x4,
    EXT4_RO_COMPAT_HUGE_FILE        = 0x8,
    EXT4_RO_COMPAT_GDT_CSUM         = 0x10,
    EXT4_RO_COMPAT_DIR_NLINK        = 0x20,
    EXT4_RO_COMPAT_EXTRA_ISIZE      = 0x40,
    EXT4_RO_COMPAT_HAS_SNAPSHOT     = 0x80,
    EXT4_RO_COMPAT_QUOTA            = 0x100,
    EXT4_RO_COMPAT_BIGALLOC         = 0x200,
    EXT4_RO_COMPAT_METADATA_CSUM    = 0x400,
    EXT4_RO_COMPAT_REPLICA          = 0x800,
    EXT4_RO_COMPAT_READONLY         = 0x1000,
    EXT4_RO_COMPAT_PROJECT          = 0x2000
};

/**
 * Enumeration of EXT4 hash algorithms.
 */
enum ext4_hash_algorithms {
    EXT4_LEGACY             = 0x0,
    EXT4_HALF_MD4           = 0x1,
    EXT4_TEA                = 0x2,
    EXT4_LEGACY_UNSIGNED    = 0x3,
    EXT4_HALF_MD4_UNSIGNED  = 0x4,
    EXT4_TEA_UNSIGNED       = 0x5,
};

/**
 * Enumeration of EXT4 mount options.
 */
enum ext4_mount_opts {
    EXT4_MNT_OPT_DEBUG          = 0x0001,
    EXT4_MNT_OPT_BSDGROUPS      = 0x0002,
    EXT4_MNT_OPT_XATTR_USER     = 0x0004,
    EXT4_MNT_OPT_ACL            = 0x0008,
    EXT4_MNT_OPT_UID16          = 0x0010,
    EXT4_MNT_OPT_JMODE_DATA     = 0x0020,
    EXT4_MNT_OPT_JMODE_ORDERED  = 0x0040,
    EXT4_MNT_OPT_JMODE_WBACK    = 0x0060,
    EXT4_MNT_OPT_NOBARRIER      = 0x0100,
    EXT4_MNT_OPT_BLOCK_VALIDITY = 0x0200,
    EXT4_MNT_OPT_DISCARD        = 0x0400,
    EXT4_MNT_OPT_NODELALLOC     = 0x0800,
};

/**
 * Enumeration of miscellaneous EXT4 flags.
 */
enum ext4_misc_flags {
    EXT4_SIGNED_DIR_HASH    = 0x0001,
    EXT4_UNSIGNED_DIR_HASH  = 0x0002,
    EXT4_DEV_CODE_TEST      = 0x0004
};

/**
 * Constructor for EXT4 superblock struct.
 */
static struct ext4_superblock *
new_superblock(void)
{
    int status;
    struct ext4_superblock *sb = NULL;

    sb = kmalloc(1024, GFP_KERNEL);
    afs_action(!IS_ERR(sb), status = PTR_ERR(sb), err_free_sb,
        "couldn't allocate sb [%d]", status);

    return sb;

err_free_sb:
    if (sb) kfree(sb);
    sb = NULL;
    return NULL;
}

/**
 * Destructor for EXT4 superblock struct.
 *
 * @sb  An EXT4 superblock struct.
 */
static void
del_superblock(struct ext4_superblock *sb)
{
    if (sb) {
        kfree(sb);
        sb = NULL;
    }
}

/**
 * Debug print function for EXT4 superblock struct.
 */
static void
print_superblock(struct ext4_superblock *sb)
{
    uint64_t blk_sz;
    uint64_t cluster_size;

    if (!sb) return;

    blk_sz = 1 << (10 + sb->s_log_blk_sz);
    cluster_size = 1 << (10 + sb->s_log_cluster_size);

    afs_debug("Filesystem volume name: %.*s", 16, sb->s_volume_name);
    afs_debug("Last mounted on: %.*s", 64, sb->s_last_mounted);
    afs_debug("Filesystem magic number: 0x%4X", sb->s_magic);
    afs_debug("Filesystem revision #: %d", sb->s_rev_level);

    afs_debug("Filesystem compatible features:");
    if (sb->s_feature_compat & EXT4_COMPAT_DIR_PREALLOC)
        afs_debug("prealloc");
    if (sb->s_feature_compat & EXT4_COMPAT_IMAGIC_INODES)
        afs_debug("imagic_inodes");
    if (sb->s_feature_compat & EXT4_COMPAT_HAS_JOURNAL)
        afs_debug("has_journal");
    if (sb->s_feature_compat & EXT4_COMPAT_EXT_ATTR)
        afs_debug("ext_attr");
    if (sb->s_feature_compat & EXT4_COMPAT_RESIZE_INODE)
        afs_debug("resize_inode");
    if (sb->s_feature_compat & EXT4_COMPAT_DIR_INDEX)
        afs_debug("dir_index");
    if (sb->s_feature_compat & EXT4_COMPAT_LAZY_BG)
        afs_debug("lazy_bg");
    if (sb->s_feature_compat & EXT4_COMPAT_EXCLUDE_INODE)
        afs_debug("exclude_inode");
    if (sb->s_feature_compat & EXT4_COMPAT_EXCLUDE_BITMAP)
        afs_debug("exclude_bitmap");
    if (sb->s_feature_compat & EXT4_COMPAT_SPARSE_SUPER2)
        afs_debug("sparse_super2");

    afs_debug("Filesystem compatible features:");
    if (sb->s_feature_incompat & EXT4_INCOMPAT_COMPRESSION)
        afs_debug("compression");
    if (sb->s_feature_incompat & EXT4_INCOMPAT_FILETYPE)
        afs_debug("filetype");
    if (sb->s_feature_incompat & EXT4_INCOMPAT_RECOVER)
        afs_debug("recover");
    if (sb->s_feature_incompat & EXT4_INCOMPAT_JOURNAL_DEV)
        afs_debug("journal_dev");
    if (sb->s_feature_incompat & EXT4_INCOMPAT_META_BG)
        afs_debug("meta_bg");
    if (sb->s_feature_incompat & EXT4_INCOMPAT_EXTENTS)
        afs_debug("extents");
    if (sb->s_feature_incompat & EXT4_INCOMPAT_64BIT)
        afs_debug("64bit");
    if (sb->s_feature_incompat & EXT4_INCOMPAT_MMP)
        afs_debug("mmp");
    if (sb->s_feature_incompat & EXT4_INCOMPAT_FLEX_BG)
        afs_debug("flex_bg");
    if (sb->s_feature_incompat & EXT4_INCOMPAT_EA_INODE)
        afs_debug("ea_inode");
    if (sb->s_feature_incompat & EXT4_INCOMPAT_DIRDATA)
        afs_debug("dir_datat");
    if (sb->s_feature_incompat & EXT4_INCOMPAT_CSUM_SEED)
        afs_debug("csum_seed");
    if (sb->s_feature_incompat & EXT4_INCOMPAT_LARGEDIR)
        afs_debug("large_dir");
    if (sb->s_feature_incompat & EXT4_INCOMPAT_INLINE_DATA)
        afs_debug("inline_data");
    if (sb->s_feature_incompat & EXT4_INCOMPAT_ENCRYPT)
        afs_debug("encrypt");

    afs_debug("Filesystem compatible features:");
    if (sb->s_feature_ro_compat & EXT4_RO_COMPAT_SPARSE_SUPER)
        afs_debug("sparse_super");
    if (sb->s_feature_ro_compat & EXT4_RO_COMPAT_LARGE_FILE)
        afs_debug("large_file");
    if (sb->s_feature_ro_compat & EXT4_RO_COMPAT_BTREE_DIR)
        afs_debug("btree_dir");
    if (sb->s_feature_ro_compat & EXT4_RO_COMPAT_HUGE_FILE)
        afs_debug("huge_file");
    if (sb->s_feature_ro_compat & EXT4_RO_COMPAT_GDT_CSUM)
        afs_debug("gdt_csum");
    if (sb->s_feature_ro_compat & EXT4_RO_COMPAT_DIR_NLINK)
        afs_debug("dir_nlink");
    if (sb->s_feature_ro_compat & EXT4_RO_COMPAT_EXTRA_ISIZE)
        afs_debug("extra_isize");
    if (sb->s_feature_ro_compat & EXT4_RO_COMPAT_HAS_SNAPSHOT)
        afs_debug("has_snapshot");
    if (sb->s_feature_ro_compat & EXT4_RO_COMPAT_QUOTA)
        afs_debug("quota");
    if (sb->s_feature_ro_compat & EXT4_RO_COMPAT_BIGALLOC)
        afs_debug("big_alloc");
    if (sb->s_feature_ro_compat & EXT4_RO_COMPAT_METADATA_CSUM)
        afs_debug("metadata_csum");
    if (sb->s_feature_ro_compat & EXT4_RO_COMPAT_REPLICA)
        afs_debug("replica");
    if (sb->s_feature_ro_compat & EXT4_RO_COMPAT_READONLY)
        afs_debug("read-only");
    if (sb->s_feature_ro_compat & EXT4_RO_COMPAT_PROJECT)
        afs_debug("project");

    afs_debug("Filesystem miscellaneous flags:");
    if (sb->s_flags & EXT4_SIGNED_DIR_HASH)
        afs_debug("signed_directory_hash");
    if (sb->s_flags & EXT4_UNSIGNED_DIR_HASH)
        afs_debug("unsigned_directory_hash");
    if (sb->s_flags & EXT4_DEV_CODE_TEST)
        afs_debug("test_development_code");

    afs_debug("Default mount options:");
    if (sb->s_default_mount_opts & EXT4_MNT_OPT_DEBUG)
        afs_debug("debug");
    if (sb->s_default_mount_opts & EXT4_MNT_OPT_BSDGROUPS)
        afs_debug("bsd_groups");
    if (sb->s_default_mount_opts & EXT4_MNT_OPT_XATTR_USER)
        afs_debug("xattr_user");
    if (sb->s_default_mount_opts & EXT4_MNT_OPT_ACL)
        afs_debug("acl");
    if (sb->s_default_mount_opts & EXT4_MNT_OPT_UID16)
        afs_debug("uid16");
    if (sb->s_default_mount_opts & EXT4_MNT_OPT_JMODE_DATA)
        afs_debug("jmode_data");
    if (sb->s_default_mount_opts & EXT4_MNT_OPT_JMODE_ORDERED)
        afs_debug("jmode_ordered");
    if (sb->s_default_mount_opts & EXT4_MNT_OPT_JMODE_WBACK)
        afs_debug("jmode_wback");
    if (sb->s_default_mount_opts & EXT4_MNT_OPT_NOBARRIER)
        afs_debug("no_barrier");
    if (sb->s_default_mount_opts & EXT4_MNT_OPT_BLOCK_VALIDITY)
        afs_debug("block_validity");
    if (sb->s_default_mount_opts & EXT4_MNT_OPT_DISCARD)
        afs_debug("discard");
    if (sb->s_default_mount_opts & EXT4_MNT_OPT_NODELALLOC)
        afs_debug("no_delayed_alloc");

    if (sb->s_creator_os == LINUX)
        afs_debug("Filesystem OS type: Linux");
    else if (sb->s_creator_os == HURD)
        afs_debug("Filesystem OS type: Hurd");
    else if (sb->s_creator_os == MASIX)
        afs_debug("Filesystem OS type: Masix");
    else if (sb->s_creator_os == FREEBSD)
        afs_debug("Filesystem OS type: FreeBSD");
    else if (sb->s_creator_os == LITES)
        afs_debug("Filesystem OS type: Lites");

    afs_debug("Inode count: %d", sb->s_inodes_count);

    if (!is_64bit) {
        afs_debug("Block count: %d", sb->s_blocks_count_lo);
    } else {
        afs_debug("Block count: %lld",
            lo_hi_64(sb->s_blocks_count_lo, sb->s_blocks_count_hi));
    }

    afs_debug("Reserved block count: %d", sb->s_r_blocks_count_lo);
    afs_debug("Free blocks: %d", sb->s_free_blocks_count_lo);
    afs_debug("Free inodes: %d", sb->s_free_inodes_count);
    afs_debug("First block: %d", sb->s_first_data_block);
    afs_debug("Block size: %lld", blk_sz);
    afs_debug("Cluster size: %lld", cluster_size);
    afs_debug("Reserved GDT blocks: %d", sb->s_reserved_gdt_blocks);
    afs_debug("Blocks per group: %d", sb->s_blocks_per_group);
    afs_debug("Clusters per group: %d", sb->s_clusters_per_group);
    afs_debug("Inodes per group: %d", sb->s_inodes_per_group);
    afs_debug("Flex block group size: %d", 1 << sb->s_log_groups_per_flex);
    afs_debug("Group descriptor size: %d", sb->s_desc_size);
    afs_debug("Inode size: %d", sb->s_inode_size);
    afs_debug("Required extra isize: %d", sb->s_min_extra_isize);
    afs_debug("Desired extra isize: %d", sb->s_want_extra_isize);
    afs_debug("Journal inode: %d", sb->s_journal_inum);

    if (sb->s_def_hash_version == EXT4_LEGACY)
        afs_debug("Default directory hash: legacy");
    else if (sb->s_def_hash_version == EXT4_HALF_MD4)
        afs_debug("Default directory hash: half_md4");
    else if (sb->s_def_hash_version == EXT4_TEA)
        afs_debug("Default directory hash: tea");
    else if (sb->s_def_hash_version == EXT4_LEGACY_UNSIGNED)
        afs_debug("Default directory hash: legacy_unsigned");
    else if (sb->s_def_hash_version == EXT4_HALF_MD4_UNSIGNED)
        afs_debug("Default directory hash: half_md4_unsigned");
    else if (sb->s_def_hash_version == EXT4_TEA_UNSIGNED)
        afs_debug("Default directory hash: tea_unsigned");
}

/**
 * EXT4 block group descriptor struct.
 */
struct ext4_group_desc {
    __le32  bg_block_bitmap_lo;         // Blocks bitmap block
    __le32  bg_inode_bitmap_lo;         // Inodes bitmap block
    __le32  bg_inode_table_lo;          // Inodes table block
    __le16  bg_free_blocks_count_lo;    // Free blocks count
    __le16  bg_free_inodes_count_lo;    // Free inodes count
    __le16  bg_used_dirs_count_lo;      // Directories count
    __le16  bg_flags;                   // EXT4_BG_flags (INODE_UNINIT, etc)
    __le32  bg_exclude_bitmap_lo;       // Exclude bitmap for snapshots
    __le16  bg_block_bitmap_csum_lo;    // crc32c(s_uuid+grp_num+bbitmap) LE
    __le16  bg_inode_bitmap_csum_lo;    // crc32c(s_uuid+grp_num+ibitmap) LE
    __le16  bg_itable_unused_lo;        // Unused inodes count
    __le16  bg_checksum;                // crc16(sb_uuid+group+desc)

    // The following fields only exist if 64-bit is enabled and if
    // superblock->s_desc_size > 32
    __le32  bg_block_bitmap_hi;         // Blocks bitmap block MSB
    __le32  bg_inode_bitmap_hi;         // Inodes bitmap block MSB
    __le32  bg_inode_table_hi;          // Inodes table block MSB
    __le16  bg_free_blocks_count_hi;    // Free blocks count MSB
    __le16  bg_free_inodes_count_hi;    // Free inodes count MSB
    __le16  bg_used_dirs_count_hi;      // Directories count MSB
    __le16  bg_itable_unused_hi;        // Unused inodes count MSB
    __le32  bg_exclude_bitmap_hi;       // Exclude bitmap block MSB
    __le16  bg_block_bitmap_csum_hi;    // crc32c(s_uuid+grp_num+bbitmap) BE
    __le16  bg_inode_bitmap_csum_hi;    // crc32c(s_uuid+grp_num+ibitmap) BE
    __u32   bg_reserved;                // Padding to 64 bytes.
} __attribute__((packed));

/**
 * Enumeration of EXT4 group descriptor flags.
 */
enum ext4_gd_flags {
    INODE_UNINIT    = 0x1,
    BLOCK_UNINIT    = 0x2,
    ITABLE_ZEROED   = 0x4
};

/**
 * Constructor for EXT4 group descriptor struct.
 */
static struct ext4_group_desc *
new_group_desc(struct ext4_superblock *sb)
{
    int status;
    struct ext4_group_desc *gd = NULL;

    if (is_64bit) gd = kmalloc(sb->s_desc_size, GFP_KERNEL);
    else gd = kmalloc(32, GFP_KERNEL);
    afs_action(!IS_ERR(gd), status = PTR_ERR(gd), err_free_gd,
        "couldn't allocate gd [%d]", status);

    return gd;

err_free_gd:
    if (gd) kfree(gd);
    gd = NULL;
    return NULL;
}

/**
 * Destructor for EXT4 group descriptor struct.
 *
 * @gd  An EXT4 group descriptor struct.
 */
static void
del_group_desc(struct ext4_group_desc *gd)
{
    if (gd) {
        kfree(gd);
        gd = NULL;
    }
}

/**
 * Debug function to print out an EXT4 group descriptor struct.
 *
 * @gd    The group descriptor to be printed.
 */
static void
print_group_desc(struct ext4_group_desc *gd, struct ext4_superblock *sb,
    uint64_t grp_num)
{
    uint64_t block_start = 0;
    uint64_t block_end = 0;
    uint64_t bmap_block = 0;
    uint64_t imap_block = 0;
    uint64_t bmap_offset = 0;
    uint64_t imap_offset = 0;
    uint64_t flex_grp_num = 0;
    uint64_t flex_group_size = 0;
    uint64_t block_bmap = 0;
    uint64_t inode_bmap = 0;
    uint32_t block_bmap_csum = 0;
    uint32_t inode_bmap_csum = 0;
    uint32_t free_blocks = 0;
    uint32_t free_inodes = 0;

    if (!gd) return;

    block_start = grp_num * sb->s_blocks_per_group;
    block_end = block_start + sb->s_blocks_per_group - 1;

    flex_group_size = 1 << sb->s_log_groups_per_flex;
    flex_grp_num = (grp_num / flex_group_size) * flex_group_size;

    if (is_64bit) {
        free_blocks = lo_hi_32(gd->bg_free_blocks_count_lo,
                               gd->bg_free_blocks_count_hi);
        free_inodes = lo_hi_32(gd->bg_free_inodes_count_lo,
                               gd->bg_free_inodes_count_hi);
        block_bmap = lo_hi_64(gd->bg_block_bitmap_lo, gd->bg_block_bitmap_hi);
        inode_bmap = lo_hi_64(gd->bg_inode_bitmap_lo, gd->bg_inode_bitmap_hi);
        block_bmap_csum = lo_hi_32(gd->bg_block_bitmap_csum_lo,
                                   gd->bg_block_bitmap_csum_hi);
        inode_bmap_csum = lo_hi_32(gd->bg_inode_bitmap_csum_lo,
                                   gd->bg_inode_bitmap_csum_hi);
    } else {
        free_blocks = gd->bg_free_blocks_count_lo;
        free_inodes = gd->bg_free_inodes_count_lo;
        block_bmap = gd->bg_block_bitmap_lo;
        inode_bmap = gd->bg_inode_bitmap_lo;
        block_bmap_csum = gd->bg_block_bitmap_csum_lo;
        inode_bmap_csum = gd->bg_inode_bitmap_csum_lo;
    }

    bmap_block = sb->s_blocks_per_group * flex_grp_num;
    bmap_offset = block_bmap - bmap_block;

    imap_block = sb->s_blocks_per_group * flex_grp_num;
    imap_offset = inode_bmap - imap_block;

    //afs_debug("Group %lld: (Blocks: %lld-%lld) csum 0x%04X",
    //    grp_num, block_start, block_end, gd->bg_checksum);

    /* afs_debug("Group descriptor flags:"); */
    /* if (gd->bg_flags & INODE_UNINIT) */
    /*     afs_debug("INODE_UNINIT"); */
    /* if (gd->bg_flags & BLOCK_UNINIT) */
    /*     afs_debug("BLOCK_UNINIT"); */
    /* if (gd->bg_flags & ITABLE_ZEROED) */
    /*     afs_debug("ITABLE_ZEROED"); */

    //afs_debug("  Block bitmap at %lld (bg #%lld + %lld), csum 0x%04X",
    //    block_bmap, flex_grp_num, bmap_offset, block_bmap_csum);

    //afs_debug("  Inode bitmap at %lld (bg #%lld + %lld), csum 0x%04X",
    //    inode_bmap, flex_grp_num, imap_offset, inode_bmap_csum);

    //afs_debug("  %d free blocks, %d free inodes", free_blocks, free_inodes);
}

/**
 * Summary of EXT4 disk (not everything; just what is needed for Artifice).
 *
 */
struct ext4_disk {
    uint32_t first_data_block;          // First data block index.
    uint64_t block_count;               // # of blocks on disk.
    uint32_t free_block_count;          // # of free blocks on disk.
    uint16_t reserved_gdt_blocks;       // # of reserved GDT blocks.
    uint64_t blk_sz;                    // Size of a block in bytes.
    uint64_t cluster_size;              // Size of a cluster in bytes.
    uint64_t num_grp_descs;             // # of group descriptors.
    uint16_t grp_desc_sz;               // Size of group descriptors.
    uint32_t blks_per_grp;              // # of blocks per group.
    bool is_64bit;                      // Flag for 64-bit support.
    bool is_sparse_super;               // Flag for redundant superblock copies.
    struct ext4_group_desc **gd_arr;    // Array of group descriptors.
};

/**
 * Constructor for an array of EXT4 group descriptors.
 *
 * @disk    The device with an EXT4 filesystem.
 */
static struct ext4_group_desc **
new_group_desc_arr(struct ext4_superblock *sb, uint64_t num_grp_descs)
{
    int status;
    uint64_t i;
    struct ext4_group_desc *gd = NULL;
    struct ext4_group_desc **gd_arr = NULL;

    gd_arr = vmalloc(num_grp_descs * sizeof(*gd));
    afs_action(!IS_ERR(gd_arr), status = PTR_ERR(gd_arr), err_free_gd_arr,
        "couldn't allocate gd_arr [%d]", status);

    for (i = 0; i < num_grp_descs; ++i) {
        gd_arr[i] = new_group_desc(sb);
        if (!gd_arr[i]) {
            //afs_debug("Failed to allocate group descriptor array!");
            goto err_free_gd_arr;
        }
    }

    return gd_arr;

err_free_gd_arr:
    if (gd_arr) {
        for (i = 0; i < num_grp_descs; ++i)
            del_group_desc(gd_arr[i]);

        vfree(gd_arr);
    }

    gd_arr = NULL;
    return NULL;
}

/**
 * Destructor for an array of EXT4 group descriptors.
 *
 * @gd_arr  The array of group descriptors to be freed.
 */
static void
del_gd_arr(struct ext4_disk *disk)
{
    int i;

    if (disk->gd_arr) {
        for (i = 0; i < disk->num_grp_descs; ++i)
            del_group_desc(disk->gd_arr[i]);

        vfree(disk->gd_arr);
        disk->gd_arr = NULL;
    }
}

/**
 * Debug function to print out a linked group descriptor list.
 *
 * @disk    Contains the pointer to the group descriptor array.
 */
static void
print_gd_arr(struct ext4_disk *disk, struct ext4_superblock *sb)
{
    uint64_t i;

    for (i = 0; i < disk->num_grp_descs; ++i)
        print_group_desc(disk->gd_arr[i], sb, i);
}


/**
 * Constructor for EXT4 disk struct.
 */
static struct ext4_disk *
new_disk(struct ext4_superblock *sb)
{
    int status;
    uint64_t num_grp_descs;
    struct ext4_disk *disk = NULL;

    disk = vmalloc(sizeof(*disk));
    afs_action(!IS_ERR(disk), status = PTR_ERR(disk), err_free_disk,
        "couldn't allocate disk [%d]", status);

    disk->first_data_block = sb->s_first_data_block;
    disk->reserved_gdt_blocks = sb->s_reserved_gdt_blocks;
    disk->free_block_count = sb->s_free_blocks_count_lo;
    disk->block_count = sb->s_blocks_count_lo;
    disk->blk_sz = 1 << (10 + sb->s_log_blk_sz);
    disk->blks_per_grp = sb->s_blocks_per_group;
    disk->cluster_size = 1 << (10 + sb->s_log_cluster_size);

    // Round to next group descriptor if necessary.
    if (sb->s_free_blocks_count_lo % sb->s_blocks_per_group == 0)
        num_grp_descs = sb->s_blocks_count_lo / sb->s_blocks_per_group;
    else
        num_grp_descs = sb->s_blocks_count_lo / sb->s_blocks_per_group + 1;
    disk->num_grp_descs = num_grp_descs;

    // Check if this device supports redundant superblock copies.
    if (sb->s_feature_ro_compat & EXT4_RO_COMPAT_SPARSE_SUPER)
        disk->is_sparse_super = true;
    else
        disk->is_sparse_super = false;

    if (is_64bit)
        disk->grp_desc_sz = sb->s_desc_size;
    else
        disk->grp_desc_sz = 32;

    disk->gd_arr = new_group_desc_arr(sb, num_grp_descs);
    if (!disk->gd_arr) {
        afs_debug("Failed to alloate group descriptor array!");
        goto err_free_disk;
    }

    return disk;

err_free_disk:
    vfree(disk);
    disk = NULL;
    return NULL;
}

/**
 * Destructor for EXT4 disk struct.
 *
 * @disk    The EXT4 disk struct to be freed.
 */
static void
del_disk(struct ext4_disk *disk)
{
    if (disk) {
        del_gd_arr(disk);
        vfree(disk);
        disk = NULL;
    }
}

/**
 * Takes a block of data and reads it into a superblock.
 *
 * @sb      A pointer to an allocated struct ext4_superblock.
 * @data    The first 4KB of the devce.
 * @return  0 == success, !0 == failure
 */
int
read_superblock(struct ext4_superblock *sb, const void *data)
{
    afs_debug("Reading data into EXT4 superblock!");
    memcpy(sb, data + EXT4_GROUP0_PAD, 1024);

    if (sb->s_magic != 0xEF53) {
        afs_debug("Device not EXT4!");
        return 1;
    }

    // Check if this device is 64-bit since EXT4 was verified.
    if (sb->s_feature_incompat & EXT4_INCOMPAT_64BIT) {
        afs_debug("Found 64-bit EXT4");
        is_64bit = true;
    } else {
        afs_debug("Found 32-bit EXT4");
    }

    //print_superblock(sb);

    return 0;
}

/**
 * Reads 4KB pages of data, splits them into blocks/group descriptors.
 *
 * @device      Device in which data is read from (usually the disk).
 */
static int
read_group_descs(struct ext4_superblock *sb, struct ext4_disk *disk,
    struct block_device *device)
{
    uint64_t i;
    uint64_t j;
    uint64_t k;
    uint64_t curr;  // Keeps track of current group descriptor index.
    uint64_t gd_blks;
    uint64_t rem_blk_sz;
    uint64_t rem_gds;
    uint64_t num_gds_per_blk;
    int status;
    int sector_offset = AFS_SECTORS_PER_BLOCK;

    char *buf = vmalloc(AFS_BLOCK_SIZE);
    afs_action(!IS_ERR(buf), status = PTR_ERR(buf),
        err_free_buf, "couldn't allocate buf [%d]", status);

    // Group descriptor size is 32B. Block size is 4096B.
    // Thus the maximum of group descriptors per block is 128.
    num_gds_per_blk = disk->blk_sz / disk->grp_desc_sz;

    // gd_blks: Number of blocks full of group descriptors there are.
    // rem_gds: Remaining group descriptors that don't take up an entire block.
    gd_blks = (disk->num_grp_descs * disk->grp_desc_sz) / disk->blk_sz;
    rem_blk_sz = (disk->num_grp_descs * disk->grp_desc_sz) % disk->blk_sz;
    rem_gds = rem_blk_sz / disk->grp_desc_sz;

    // Read in group descriptors 128 at a time.
    for (i = 0; i < gd_blks; ++i) {
        memset(buf, 0, AFS_BLOCK_SIZE);
        read_page(buf, device, 0, sector_offset, true);

        for (j = 0; j < num_gds_per_blk; ++j) {
            curr = j + (i * num_gds_per_blk);

            disk->gd_arr[curr] = new_group_desc(sb);
            afs_action(!IS_ERR(disk->gd_arr[curr]),
                status = PTR_ERR(disk->gd_arr[curr]),
                err_free_gd_arr, "couldn't allocate gd_arr[%lld] [%d]",
                curr, status);

            memcpy(disk->gd_arr[curr], buf + (disk->grp_desc_sz * j),
                disk->grp_desc_sz);
        }

        sector_offset += AFS_SECTORS_PER_BLOCK;     // 8 sectors per block
    }

    // Reset buffer and read in remaining group descriptors.
    memset(buf, 0, AFS_BLOCK_SIZE);
    read_page(buf, device, 0, sector_offset, true);

    for (k = 0; k < rem_gds; ++k) {
        curr = k + (gd_blks * num_gds_per_blk);

        disk->gd_arr[curr] = new_group_desc(sb);
        afs_action(!IS_ERR(disk->gd_arr[curr]),
            status = PTR_ERR(disk->gd_arr[curr]),
            err_free_gd_arr, "couldn't allocate gd_arr[%lld] [%d]",
            curr, status);

        memcpy(disk->gd_arr[curr], buf + (disk->grp_desc_sz * k),
            disk->grp_desc_sz);
    }

    vfree(buf);
    buf = NULL;
    return 0;

err_free_gd_arr:
    del_gd_arr(disk);

err_free_buf:
    vfree(buf);
    buf = NULL;
    return 1;
}

/**
 * Debug function to calculate and print free block ranges like dumpe2fs.
 *
 * @grp_num   The group number for the group descriptor free block range.
 * @bvec        The bit vector representing the group's free/used blocks.
 * @offset      The offset from the first data block.
 */
static void
print_free_range(uint64_t grp_num, bit_vector_t *bvec, uint32_t offset,
    struct ext4_disk *disk, struct ext4_superblock *sb)
{
    uint64_t i;
    uint64_t j;
    uint64_t start;
    uint64_t end;

    print_group_desc(disk->gd_arr[grp_num], sb, grp_num);

    if (grp_num == 17) {
        //afs_debug("Group number 17:");

        for (i = 0; i < bvec->length; ++i) {
            if (!bit_vector_get(bvec, i)) {
                printk(KERN_CONT "0");
            } else {
                printk(KERN_CONT "1");
            }
        }

        printk("\n");
    }

    for (i = 0; i < bvec->length; ++i) {
        if (!bit_vector_get(bvec, i)) {
            start = (disk->blks_per_grp * grp_num) + i + disk->first_data_block;

            for (j = i; j < bvec->length && !bit_vector_get(bvec, j); ++j) ;
            --j;    // Minus 1 since j gets incremented an extra time.

            end = (disk->blks_per_grp * grp_num) + j + disk->first_data_block;

            if (start != end) {
                //afs_debug("  Free blocks: %lld - %lld", start, end);
	    }
            else {
                afs_debug("  Free block: %lld", start);
	    }

            i = j;
        }
    }
}

/**
 * Calculates if num is a power of n.
 *
 * @num     The number to be checked if it's a power of n.
 * @n       The number you're checking num is a power of.
 * @return  Boolean.
 */
static bool
is_pow_n(uint64_t num, uint64_t n)
{
    if (num == 0) return false;

    while (num % n == 0)
        num /= n;

    return num == 1;
}

/**
 * Reads in the block group desciptor's bitmap.
 *
 * @device      The device to with the EXT4 filesystem.
 * @gd          The group descriptor to get the bitmap for.
 * @grp_num   The group descriptor number.
 * @bvec        The bit vector to read the bitmap into.
 * @return      0 == success, !0 == failure
 */
static int
read_bitmap(struct block_device *device, struct ext4_disk *disk,
    struct ext4_group_desc *gd, struct ext4_superblock *sb,
    uint64_t grp_num, bit_vector_t *bvec)
{
    int i;
    int j;
    int status;
    bool is_pow_3 = false;
    bool is_pow_5 = false;
    bool is_pow_7 = false;
    bool has_backups = false;
    uint64_t offset = 0;        // For keeping track of block offset index.
    uint64_t gd_blks = 0;       // Blocks full of group descriptors (grp descs)
    uint64_t rem_blk_sz = 0;    // Bytes remaining in last non-filled block.
    uint64_t rem_gds = 0;       // Remaining grp descs that don't fill a block.
    uint64_t block_bmap = 0;
    uint64_t sector_offset = AFS_SECTORS_PER_BLOCK;

    char *buf = vmalloc(AFS_BLOCK_SIZE);
    afs_action(!IS_ERR(buf), status = PTR_ERR(buf),
        err_free_buf, "couldn't allocate buf [%d]", status);

    if (!device || !disk || !gd || !bvec) goto err_free_buf;

    // Recalculate where to read block if 64-bit.
    if (is_64bit) {
        block_bmap = lo_hi_64(gd->bg_block_bitmap_lo, gd->bg_block_bitmap_hi);
    } else {
        block_bmap = gd->bg_block_bitmap_lo;
    }

    memset(buf, 0, AFS_BLOCK_SIZE);
    read_page(buf, device, 0, block_bmap * sector_offset, true);

    // Set bits in bitvector accordingly.
    for (i = 0; i < disk->blks_per_grp; ++i) {
        if (buf[i / 8] & (1 << (i % 8)))
            bit_vector_set(bvec, i);
    }

    // If sparse_super flag is set, redundant superblock copies are kept in
    // groups whose number is either 0 or a power of 3, 5, or 7.
    // For some reason, EXT4 doen't already set these as in use, so we have to
    // set them ourselves.
    is_pow_3 = is_pow_n(grp_num, 3);
    is_pow_5 = is_pow_n(grp_num, 5);
    is_pow_7 = is_pow_n(grp_num, 7);
    has_backups = (grp_num == 0 || is_pow_3 || is_pow_5 || is_pow_7);

    if (disk->is_sparse_super && has_backups) {
        // Set superblock backup as in use.
        bit_vector_set(bvec, 0);
        offset = 1;

        // Calculate how many blocks are needed for group descriptors.
        gd_blks = (disk->num_grp_descs * disk->grp_desc_sz) / disk->blk_sz;
        rem_blk_sz = (disk->num_grp_descs * disk->grp_desc_sz) % disk->blk_sz;
        rem_gds = rem_blk_sz / disk->grp_desc_sz;
        if (rem_gds != 0) ++gd_blks;

        // Set group descriptor block backup as in use.
        for (j = offset; j < offset + gd_blks; ++j)
            bit_vector_set(bvec, j);

        offset += gd_blks;

        // Set reserved GDT blocks as in use.
        for (j = offset; j < offset + disk->reserved_gdt_blocks; ++j)
            bit_vector_set(bvec, j);
    }

    print_free_range(grp_num, bvec, disk->first_data_block, disk, sb);

    vfree(buf);
    return 0;

err_free_buf:
    if (buf) vfree(buf);
    buf = NULL;
    return 1;
}

/**
 * Reads in bitmaps from each block group descriptor and records free block
 * offsets into filesystem free block list.
 *
 * @disk    The summary of the device with EXT4.
 * @return  0 == success, !0 == failure
 */
static int
read_bitmaps(struct ext4_disk *disk, struct block_device *device,
    struct afs_passive_fs *fs, struct ext4_superblock *sb)
{
    int status;
    uint32_t i = 0;
    uint32_t j = 0;
    uint64_t k = 0;
    uint64_t blk_offset;
    uint64_t blk_grp_offset;
    uint32_t *block_list = NULL;
    bit_vector_t *bvec = NULL;

    bvec = bit_vector_create(disk->blks_per_grp);
    if (!bvec) {
        afs_debug("Couldn't allocate enough memory for bitvector!");
        return 1;
    }

    block_list = vmalloc(disk->free_block_count * sizeof(uint32_t));
    if (!block_list) {
        afs_debug("Couldn't allocate free block list!");
        goto err_free_bvec;
    }

    for (i = 0; i < disk->num_grp_descs; ++i) {
        status = read_bitmap(device, disk, disk->gd_arr[i], sb, i, bvec);
        if (status) {
            afs_debug("Failed to read in bitmap!");
            goto err_free_blocklist;
        }

        // Count and add free block indices.
        // Also clears out the bit vector at the same time.
        for (j = 0; j < bvec->length; ++j) {
            if (bit_vector_get(bvec, j) == 0) {
                blk_grp_offset = disk->blks_per_grp * i;
                blk_offset = j + disk->first_data_block;

                if (blk_grp_offset + blk_offset <= 0xFFFFFFFF) {
                    block_list[k++] = blk_grp_offset + blk_offset;
                }
            }

            bit_vector_clear(bvec, j);
        }
    }

    fs->list_len = k;
    fs->block_list = vmalloc(fs->list_len * sizeof(uint32_t));
    if (!fs->block_list) {
        afs_debug("Couldn't allocate free block list!");
        goto err_free_bvec;
    }

    // Copy over the temporary block list.
    memcpy(fs->block_list, block_list, k);

    vfree(block_list);
    bit_vector_free(bvec);
    return 0;

err_free_blocklist:
    vfree(block_list);
    return 1;

err_free_bvec:
    bit_vector_free(bvec);
    return 1;
}

/**
 * Detect the presence of an EXT4 file system
 * on 'device'.
 *
 * @data    The first 4KB of the device.
 * @fs      The file system information to be filled in.
 * @return  boolean.
 */
bool
afs_ext4_detect(const void *data, struct block_device *device,
    struct afs_passive_fs *fs)
{
    int status = 0;
    bool is_ext4 = false;
    struct ext4_disk *disk = NULL;
    struct ext4_superblock *sb = NULL;

    afs_debug("Detecting EXT4 on device!");

    sb = new_superblock();
    if (!sb) {
        afs_debug("Failed to create new superblock!");
        goto leave;
    }

    // If read_superblock returns successfully (0), device is EXT4.
    status = read_superblock(sb, data);
    if (status) {
        afs_debug("Failed to read EXT4 superblock!");
        goto err_free_sb;
    } else {
        is_ext4 = true;
    }

    disk = new_disk(sb);
    if (!disk) {
        afs_debug("Failed to create new disk summary!");
        goto err_free_disk;
    }

    status = read_group_descs(sb, disk, device);
    if (status) {
        afs_debug("Problem reading EXT4 group descriptors!");
        goto err_free_disk;
    }

    print_gd_arr(disk, sb);

    status = read_bitmaps(disk, device, fs, sb);
    if (status) {
        afs_debug("Problem reading bitmaps!");
        goto err_free_disk;
    }

err_free_disk:
    del_disk(disk);

err_free_sb:
    del_superblock(sb);

leave:
    return is_ext4;
}

