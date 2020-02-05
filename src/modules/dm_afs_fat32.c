/*
 * TODO: Refactor with afs_assert and afs_action.
 *
 * Author: Austen Barker <atbarker@ucsc.edu>
 * Copyright: UC Santa Cruz, SSRC
 */
#include <dm_afs.h>
#include <dm_afs_modules.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/types.h>

// All the information about a FAT volume.
struct fat_volume {
    void *fat_map;              // FAT mapped into memory.
    uint32_t *empty_clusters;   // List of empty clusters (blocks).
    uint32_t num_data_clusters; // Number of data cluster on the disk.
    off_t data_start_off;       // Byte offset for the second cluster.
    size_t num_alloc_files;     // Number of allocated files.
    size_t max_allocated_files; // Number of allocatable files.
    char oem_name[8 + 1];       // Boot sector information.
    uint32_t num_empty_clusters;

    // Data from the DOS 2.0 parameter block.
    uint16_t bytes_sector;     // Bytes in a sector.
    uint16_t sector_order;     // Sector order.
    uint8_t sec_cluster;       // Sectors in cluster.
    uint8_t sec_cluster_order; // Sector cluster order.
    uint16_t cluster_order;    // Cluster order.
    uint16_t reserved;         // Reserved sectors.
    uint8_t tables;            // Number of fat tables.
    uint16_t root_entries;     // Max root entries.
    uint8_t media_desc;        // Media description.
    uint32_t total_sec;        // Total number of sectors.
    uint32_t sec_fat;          // Sectors per FAT.

    // Data from DOS 3.0 block.
    uint16_t sec_track;  // Sectors per track.
    uint16_t num_heads;  // Number of heads.
    uint32_t hidden_sec; // Number of hidden sectors.

    // FAT32 extended.
    uint16_t driv_desc;      // Drive description.
    uint16_t version;        // Drive version.
    uint32_t root_dir_start; // Start of the root directory.
    uint16_t fs_info_sec;    // Info sector offset.
    uint16_t alt_boot_sec;   // Alternate boot sector.

    // NonFAT32 extended.
    uint8_t phys_driv_num;     // Physical drive number.
    uint8_t ext_boot_sig;      // Boot signature.
    uint32_t vol_id;           // Volume id.
    char volume_label[11 + 1]; // Volume label.
    char fs_type[8 + 1];       // FS type.
};

// Extended BIOS parameter block for FAT32.
struct __attribute__((packed)) fat32_ebpb {
    __le32 sec_fat;          // Sectors per fat.
    __le16 drive_desc;       // Drive description.
    __le16 version;          // Version.
    __le32 root_start_clust; // Starting cluster for the root dir.
    __le16 fs_info_sec;      // Info sector.
    __le16 alt_boot_sec;     // Alternate boot sector.
    uint8_t reserved[12];
};

// Extended BIOS parameter block for NonFAT32.
struct __attribute__((packed)) nonfat32_ebpb {
    uint8_t physical_drive_num;
    uint8_t reserved;
    uint8_t extended_boot_sig;
    __le32 volume_id;
    char volume_label[11];
    char fs_type[8];
};

// FAT32 boot sector. Exactly how it appears on disk.
struct fat_boot_sector {
    uint8_t jump_insn[3]; // Bootloader jump instruction.
    char oem_name[8];     // Standard information.

    // Dos 2.0 parameter block (13 bytes).
    __le16 bytes_sec;     // Bytes per sec.
    uint8_t sec_cluster;  // Sectors per cluster.
    __le16 res_sec;       // Reserved sectors.
    uint8_t num_tables;   // Number of allocation tables.
    __le16 max_root_ent;  // Maximum root entries.
    __le16 total_sectors; // Total sectors on disk.
    uint8_t media_desc;   // Media descriptor.
    __le16 sec_fat;       // Sectors per fat.

    // Dos 3.31 parameter block (12 bytes).
    __le16 sec_track;    //sectors on each track
    __le16 num_heads;    //number of heads
    __le32 hidden_sec;   //number of hidden sectors
    __le32 total_sec_32; //total sectors (32 bit number)

    // BIOS parameter block - only FAT32.
    union __attribute__((packed)) {
        struct __attribute__((packed)) {
            struct fat32_ebpb fat32_ebpb;
            struct nonfat32_ebpb nonfat32_ebpb;
        } fat32;
        struct nonfat32_ebpb nonfat32_ebpb;
    } ebpb;
} __attribute__((packed));

/**
 * Reads in boot parameter block as seen in DOS 2.0.
 */
static int
fat_read_dos_2_0_bpb(struct fat_volume *vol, const struct fat_boot_sector *boot_sec)
{
    vol->bytes_sector = le16_to_cpu(boot_sec->bytes_sec);
    vol->sector_order = bsr(vol->bytes_sector);
    afs_assert(is_power_of_2(vol->bytes_sector) && vol->sector_order >= 5 && vol->sector_order <= 12,
        out_invalid, "sector order mismatch [%d]", vol->sector_order);

    vol->sec_cluster = boot_sec->sec_cluster;
    vol->sec_cluster_order = bsr(vol->sec_cluster);
    afs_assert(is_power_of_2(vol->sec_cluster) && vol->sec_cluster_order <= 7, out_invalid,
        "sector cluster order mismatch [%d]", vol->sec_cluster_order);

    vol->cluster_order = vol->sector_order + vol->sec_cluster_order;
    vol->reserved = le16_to_cpu(boot_sec->res_sec);

    vol->tables = boot_sec->num_tables;
    afs_assert(vol->tables == 1 || vol->tables == 2, out_invalid, "incorrect number of tables");

    vol->root_entries = le16_to_cpu(boot_sec->max_root_ent);
    if (vol->root_entries == 0) {
        // then this is a FAT32 volume.
        afs_debug("FS is Fat32, not Fat16 or 12");
    } else {
        // TODO: Why is this here?
    }
    vol->total_sec = le16_to_cpu(boot_sec->total_sectors);
    vol->media_desc = boot_sec->media_desc;
    vol->sec_fat = le16_to_cpu(boot_sec->sec_fat);
    return 0;

out_invalid:
    return -1;
}

/**
 * Reads in Boot parameter block as appearing in DOS 3.31 and onward.
 *
 * @vol         Place to put the data
 * @boot_sec    the boot sector copy
 * @return      status
 */
static int
fat_read_dos_3_31_bpb(struct fat_volume *vol, const struct fat_boot_sector *boot_sec)
{
    vol->sec_track = le16_to_cpu(boot_sec->sec_track);
    vol->num_heads = le16_to_cpu(boot_sec->num_heads);
    vol->hidden_sec = le32_to_cpu(boot_sec->hidden_sec);
    if (vol->total_sec == 0) {
        vol->total_sec = le32_to_cpu(boot_sec->total_sec_32);
    } else {
        // 16 bit sectors.
    }

    return 0;
}

/**
 * Non fat32 extended boot parameter block helper.
 */
static int
fat_read_nonfat32_ebpb(struct fat_volume *vol, const struct nonfat32_ebpb *ebpb)
{
    vol->phys_driv_num = ebpb->physical_drive_num;
    vol->ext_boot_sig = ebpb->extended_boot_sig;
    vol->vol_id = le32_to_cpu(ebpb->volume_id);
    memcpy(vol->volume_label, ebpb->volume_label, sizeof(ebpb->volume_label));
    memcpy(vol->fs_type, ebpb->fs_type, sizeof(ebpb->fs_type));

    // TODO: Add debug statements.
    return 0;
}

/**
 * FAT32 extended boot parameter block helper.
 */
static int
fat_read_fat32_ebpb(struct fat_volume *vol, const struct fat32_ebpb *ebpb)
{
    if (le32_to_cpu(ebpb->sec_fat) != 0) {
        vol->sec_fat = le32_to_cpu(ebpb->sec_fat);
        if ((((size_t)vol->sec_fat << vol->sector_order) >> vol->sector_order) != (size_t)vol->sec_fat) {
            // error out.
            goto out_invalid;
        }
    }
    vol->driv_desc = le16_to_cpu(ebpb->drive_desc);
    vol->version = le16_to_cpu(ebpb->version);
    if (vol->version != 0) {
        // error.
        goto out_invalid;
    }
    vol->root_dir_start = le32_to_cpu(ebpb->root_start_clust);
    if (vol->root_dir_start == 0) {
        // invalid position for starting cluster.
        goto out_invalid;
    }
    vol->fs_info_sec = le16_to_cpu(ebpb->fs_info_sec);
    vol->alt_boot_sec = le16_to_cpu(ebpb->alt_boot_sec);
    if (vol->fs_info_sec == 0xffff) {
        vol->fs_info_sec = 0;
    }

    if (vol->fs_info_sec != 0 && vol->sector_order < 9) {
        goto out_invalid;
    }
    return 0;

out_invalid:
    return -1;
}

/**
 * Reads in parameters from the FAT superblock.
 * If this fails the file system is not FAT or
 * is corrupted.
 *
 * @vol     FAT volume summary.
 * @data    First 4KB bytes of device.
 * @return  status.
 */
static int
read_boot_sector(struct fat_volume *vol, const void *data)
{
    struct fat_boot_sector *boot_sec = NULL;
    int ret;
    uint32_t num_data_sectors;

    boot_sec = kmalloc(512, GFP_KERNEL);
    afs_action(!IS_ERR(boot_sec), ret = PTR_ERR(boot_sec), out_invalid, "could not allocate boot_sec [%d]", ret);

    // Copy in basic information from the first 512 bytes.
    memcpy(boot_sec, data, 512);
    memcpy(vol->oem_name, boot_sec->oem_name, sizeof(boot_sec->oem_name));

    // TODO: Remove trailing spaces from vol->oem_name.
    // To be honest I don't really need it.

    ret = fat_read_dos_2_0_bpb(vol, boot_sec);
    if (ret) {
        afs_debug("failed to read dos 2.0 bpb");
        goto out_invalid;
    }
    ret = fat_read_dos_3_31_bpb(vol, boot_sec);
    if (ret) {
        afs_debug("failed to read dos3.31 bpb");
        goto out_invalid;
    }

    ret = fat_read_fat32_ebpb(vol, &boot_sec->ebpb.fat32.fat32_ebpb);
    if (ret) {
        afs_debug("Failed to read fat32 ebpb");
        goto out_invalid;
    }
    ret = fat_read_nonfat32_ebpb(vol, &boot_sec->ebpb.nonfat32_ebpb);

    num_data_sectors = vol->total_sec - vol->reserved - (vol->sec_fat * vol->tables) - ((vol->root_entries << 5) >> vol->sector_order);
    afs_debug("Number of data sectors: %u", num_data_sectors);
    afs_debug("Number of reserved: %u", vol->sec_fat);

    vol->num_data_clusters = num_data_sectors / vol->sec_cluster;
    kfree(boot_sec);
    return 0;

out_invalid:
    return -1;
}

/**
 * Calculates the aligned FAT offset and size.
 * Maps the FAT to memory and finds unallocated clusters.
 *
 * @vol     Summary of the fat device.
 * @data    Superblock contents.
 * @device  Block device being read from.
 * @return  status [0 == success | !0 == fail]
 */
static int
fat_map(struct fat_volume *vol, void *data, struct block_device *device)
{
    size_t fat_size_bytes;
    size_t fat_aligned_size_bytes;
    off_t fat_offset;
    off_t fat_aligned_offset;
    uint32_t *p;
    uint32_t *empty_clusters;
    int i;
    int cluster_number;
    sector_t start_sector;
    uint8_t *fat_data = NULL;
    uint32_t page_size;
    uint32_t page_number;
    uint8_t *reader = kmalloc(4096, GFP_KERNEL);

    page_size = 1 << PAGE_SHIFT;
    fat_offset = (off_t)vol->reserved << vol->sector_order;
    fat_aligned_offset = fat_offset & ~(page_size - 1);
    fat_size_bytes = (size_t)(vol->sec_fat * vol->bytes_sector);
    fat_aligned_size_bytes = fat_size_bytes + (fat_offset - fat_aligned_offset);
    page_number = fat_aligned_size_bytes / 4096 + 1;
    vol->data_start_off = (off_t)((vol->tables * vol->sec_fat) + vol->reserved);

    afs_debug("Data start offset: %zu ", vol->data_start_off);
    afs_debug("Root starting cluster: %u", vol->root_dir_start);
    afs_debug("FAT aligned size in bytes: %zu ", fat_aligned_size_bytes);
    afs_debug("FAT aligned offset: %d ", (int)fat_aligned_offset);
    afs_debug("FAT size in pages: %u ", page_number);
    start_sector = 0;

    if (fat_aligned_offset + fat_aligned_size_bytes > 4096) {
        afs_debug("Read more data to map the FAT");
        //page = alloc_pages(GFP_KERNEL, (unsigned int)(bsr(page_number)));
        //fat_data = page_address(page);
        fat_data = vmalloc(page_number * AFS_BLOCK_SIZE);
        for(i = 0; i < page_number; i++){
            read_page(reader, device, (fat_aligned_offset / 4096) + i, 0, true);
            memcpy(&fat_data[i * 4096], reader, 4096);
        }
        //fatio.bdev = device;
        //fatio.io_page = page;
        //fatio.io_sector = fat_aligned_offset / 512;
        //fatio.io_size = fat_aligned_size_bytes;
        //fatio.type = IO_READ;
        //status = afs_blkdev_io(&fatio);
        afs_debug("FAT read successfully.");
    }
    
    vol->fat_map = fat_data;
     
    p = (int *)fat_data;
    afs_debug("p: %p", p);

    cluster_number = 0;
    empty_clusters = vmalloc(fat_size_bytes);
    memset((void *)empty_clusters, 0, fat_size_bytes);

    for (i = 0; i < vol->num_data_clusters; i++) {
        //afs_debug("block %d: %d", i, p[i]);
        if (p[i] == 0) {
            empty_clusters[cluster_number] = i;
            cluster_number++;
        }
    }
    vol->empty_clusters = empty_clusters;
    vol->num_empty_clusters = cluster_number;
    kfree(reader);
    vfree(fat_data);
    return 0;
}

/**
 * Detect the presence of a FAT32 file system
 * on 'device'.
 *
 * @data    The first 4KB of the device.
 * @fs      The file system information to be filled in.
 * @return  boolean.
 */
bool
afs_fat32_detect(const void *data, struct block_device *device, struct afs_passive_fs *fs)
{
    struct fat_volume *vol = NULL;
    int ret;

    // Allocate memory for volume.
    vol = kmalloc(sizeof(*vol), GFP_KERNEL);
    afs_action(!IS_ERR(vol), ret = PTR_ERR(vol), vol_err, "could not allocate vol [%d]", ret);

    // Read the boot sector.
    ret = read_boot_sector(vol, data);
    if (ret) {
        afs_debug("Failed to read boot sector");
        goto vol_err;
    }

    ret = fat_map(vol, (void *)data, device);
    if (ret) {
        afs_debug("Failed to map FAT");
        goto vol_err;
    }
    //vol->data_start_off = (off_t)((vol->tables * vol->sec_fat) + vol->reserved + (vol->root_entries >> (vol->sector_order - 5)))
    //    << vol->sector_order;

    vol->data_start_off = (off_t)((vol->tables * vol->sec_fat) + vol->reserved);

    //TODO Really should just store the whole volume struct but oh well
    if (fs) {
        fs->total_blocks = vol->num_data_clusters;
        fs->sectors_per_block = vol->sec_cluster;
        afs_debug("sectors per cluster %d", vol->sec_cluster);
        fs->block_list = vol->empty_clusters;
        fs->list_len = vol->num_empty_clusters;
        fs->data_start_off = vol->data_start_off; // Data start in sectors, blocks are relative to this.
	kfree(vol);
        return true;
    }

vol_err:
    kfree(vol);
    return false;
}
