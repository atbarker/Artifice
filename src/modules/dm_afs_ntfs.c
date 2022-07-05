/*
 * Author: Fill in.
 * Copyright: Fill in.
 */
#include <dm_afs.h>
#include <dm_afs_modules.h>

#include <linux/kern_levels.h>
#include <linux/vmalloc.h>

#include "/usr/lib/modules/5.18.7-arch1-1/build/include/linux/stddef.h"

#define ATTRS_DONE 0xFFFFFFFF

/**
 * Detect the presence of an NTFS file system
 * on 'device'.
 *
 * @data    The first 4KB of the device.
 * @fs      The file system information to be filled in.
 * @return  boolean.
 */

struct ntfs_bpb {
    __le16 bytes_per_sector;
    __u8   sectors_per_cluster;
    __le16 reserved_sector_count; // always 9
    __u8   table_count;           // unused, always 0
    __le16 root_entry_count;      // unused, always 0
    __le16 sector_count;          // unused, always 0
    __u8   media_type;
    __le16 sectors_per_table;     // unused, always 0
    __le16 sectors_per_track;
    __le16 heads;
    __le32 hidden_sector_count;
    __le32 sector_count_32;       // unused
    __le32 _reserved;              // unused
} __attribute__((packed));

struct ntfs_ebpb {
    __le64 sector_count_64;
    __le64 master_file_table_cluster;
    __le64 master_file_table_mirror_cluster;
    __u8   clusters_per_record;   // positive: clus per rec, negative: bytes per rec
    __u8   _reserved_200[3];
    __u8   clusters_per_index_buffer; // pos: clus per IB, neg: bytes per IB
    __u8   _reserved_216[3];
    __le64 serial_number;
    __le32 checksum;
} __attribute__((packed));

struct ntfs_boot_sector {
    int8_t   jmp[3];
    char     oem_id[8];
    struct ntfs_bpb bpb;
    struct ntfs_ebpb ebpb;
    // We are ignoring the bootstrap code
} __attribute__((packed));

struct mft_header {
    char record_signature[4];
    __le16 offset_to_update_seq;
    __le16 entries_in_fixup_array;
    __le64 logfile_sequence_number;
    __le16 record_usage_count;
    __le16 hardlink_count;
    __le16 offset_to_first_attribute;
    __le16 flags;
    __le32 size_of_record;
    __le32 allocated_size_of_record;
    __le64 base_file_record;
    __le16 next_attr_id;
    __le16 unused;
    __le32 mft_record_number;
} __attribute__((packed));

struct attribute_header {
    __le32 type_id;
    __le32 length;
    __u8 nonresident_flag;
    __u8 name_length;
    __le16 offset_to_name;
    __le16 flags;
} __attribute((packed));

struct attribute_resident {
    struct attribute_header header;
    __le16 attribute_id;
    __le32 content_length;
    __le16 offset_to_content;
} __attribute((packed));

struct attribute_nonresident {
    struct attribute_header header;
    __le16 attribute_id;
    __le64 start_vcn_runlist;
    __le64 end_vcn_runlist;
    __le16 offset_to_runlist;
    __le16 compression_unit_size;
    __le32 _reserved;
    __le64 content_allocated_size;
    __le64 content_actual_size;
    __le64 content_initialized_size;
} __attribute((packed));

struct ntfs_volume {
    uint16_t bytes_per_sector;
    uint64_t sector_count;
    uint64_t cluster_count;
    uint8_t  sectors_per_cluster;
    uint64_t bytes_per_cluster;

    uint64_t mft_cluster;
    uint64_t mft_mirror_cluster;

    uint32_t mft_record_size;

    uint8_t  afs_blocks_per_cluster;
    uint8_t mft_records_per_cluster;

    uint32_t num_afs_blocks;
    uint8_t  afs_sectors_per_cluster;
    uint32_t *empty_blocks;
    uint32_t num_empty_afs_blocks;
    off_t    data_start_off;

    // Metafiles (as records).
    struct mft_header *metafiles[12];
};

enum metafile {
    $mft      = 0,
    $mft_mirr = 1,
    $log_file = 2,
    $volume   = 3,
    $attr_def = 4,
    $root_dir = 5,
    $bitmap   = 6,
    $boot     = 7,
    $bad_clus = 8,
    $secure   = 9,
    $up_case  = 10,
    $extend   = 11
};

static const char ntfs_magic[] = "NTFS    ";

bool is_ntfs_magic_value(char *oem_name) {
    return strncmp(oem_name, ntfs_magic, 8) == 0;
}

static int
read_boot_sector(struct ntfs_volume *vol, const void *data)
{
    char oem_name[9] = { 0 };
    struct ntfs_boot_sector *boot_sector = (struct ntfs_boot_sector*)data;

    uint16_t reserved_sector_count;

    memcpy(oem_name, boot_sector->oem_id, 8);
    afs_debug("Got NTFS OEM name: %s", oem_name);
    afs_assert(is_ntfs_magic_value(oem_name), boot_sector_invalid,
            "OEM name: '%s' != '%s'", oem_name, ntfs_magic);

    reserved_sector_count = le16_to_cpu(boot_sector->bpb.reserved_sector_count);

    afs_assert(boot_sector->bpb.table_count == 0, boot_sector_invalid,
            "Table count: %d != 0", boot_sector->bpb.table_count);

    afs_assert(boot_sector->bpb.root_entry_count == 0, boot_sector_invalid,
            "Root entry count: %d != 0", boot_sector->bpb.root_entry_count);

    afs_assert(boot_sector->bpb.sector_count == 0, boot_sector_invalid,
            "Sector count: %d != 0", boot_sector->bpb.sector_count);

    afs_assert(boot_sector->bpb.sectors_per_table == 0, boot_sector_invalid,
            "Sectors per table: %d != 0", boot_sector->bpb.sectors_per_table);

    vol->sector_count = le64_to_cpu(boot_sector->ebpb.sector_count_64);
    vol->bytes_per_sector = le16_to_cpu(boot_sector->bpb.bytes_per_sector);
    vol->sectors_per_cluster = boot_sector->bpb.sectors_per_cluster;
    vol->bytes_per_cluster = vol->bytes_per_sector * vol->sectors_per_cluster;
    vol->mft_cluster = le64_to_cpu(boot_sector->ebpb.master_file_table_cluster);
    vol->mft_mirror_cluster = le64_to_cpu(boot_sector->ebpb.master_file_table_mirror_cluster);
    vol->cluster_count = vol->sector_count / vol->sectors_per_cluster;

    // If `clusters_per_record` > 0, it is really the number of clusters
    // per record. If it is < 0, the real number of clusters per record is
    // 2^`clusters_per_record`. The true value is stored in `mft_record_size`.
    if (boot_sector->ebpb.clusters_per_record > 0) {
        vol->mft_record_size = vol->bytes_per_cluster * boot_sector->ebpb.clusters_per_record;
    } else {
        vol->mft_record_size =  1 << (-boot_sector->ebpb.clusters_per_record);
    }

    // This driver cannot read NTFS clusters if the size is not a
    // multiple of the AFS_BLOCK_SIZE.
    if (vol->bytes_per_cluster % AFS_BLOCK_SIZE) {
        afs_assert(false, boot_sector_invalid,
                "NTFS volume incompatible with Artifice: invalid block size [%llu]",
                vol->bytes_per_cluster);
    }

    vol->afs_sectors_per_cluster = vol->bytes_per_cluster / AFS_SECTOR_SIZE;
    vol->afs_blocks_per_cluster = vol->bytes_per_cluster / AFS_BLOCK_SIZE;
    vol->mft_records_per_cluster = vol->bytes_per_cluster / vol->mft_record_size;

    afs_debug("Found valid boot sector for NTFS volume of size: %lld bytes",
            vol->sector_count * vol->bytes_per_sector);

    vol->num_afs_blocks = vol->cluster_count * vol->afs_blocks_per_cluster;

    return 0;
boot_sector_invalid:
    return 1;
}

int read_ntfs_cluster(void *page, struct ntfs_volume *vol, struct block_device *device, uint64_t cluster_number, bool used_vmalloc) {
    int ret;
    int block_offset;
    uint32_t block_num = cluster_number * vol->afs_blocks_per_cluster;
    for (block_offset = 0; block_offset < vol->afs_blocks_per_cluster; block_offset += AFS_BLOCK_SIZE) {
        ret = read_page((char*)page + block_offset, device,
                block_num + block_offset, 0, used_vmalloc);
        if (ret)
            return ret;
    }
    return 0;
}


enum ntfs_attributes {
    $UNUSED                 = 0x00,
    $STANDARD_INFORMATION   = 0x10,
    $ATTRIBUTE_LIST         = 0x20,
    $FILE_NAME              = 0x30,
    $OBJECT_ID              = 0x40,
    $SECURITY_DESCRIPTOR    = 0x50,
    $VOLUME_NAME            = 0x60,
    $VOLUME_INFORMATION     = 0x70,
    $DATA                   = 0x80,
    $INDEX_ROOT             = 0x90,
    $INDEX_ALLOCATION       = 0xA0,
    $BITMAP                 = 0xB0,
    $REPARSE_POINT          = 0xC0,
    $EA_INFORMATION         = 0xD0,
    $EA                     = 0xE0,
    $PROPERTY_SET           = 0xF0,
    $LOGGED_UTILITY_STREAM  = 0x100
};

static int
read_mft_records(char *record, struct ntfs_volume *vol, struct block_device *device, uint32_t number, bool used_vmalloc) {
    int ret;
    uint64_t cluster;

    afs_assert(number % vol->mft_records_per_cluster == 0,
            unaligned_record, "Asked for MFT record that is not aligned: %d", number);

    cluster = vol->mft_cluster + number / vol->mft_records_per_cluster;
    ret = read_ntfs_cluster(record, vol, device, cluster, used_vmalloc);
    if (ret)
        return ret;
    return 0;

unaligned_record:
    return 1;
}

#define MIN(x, y) (x < y ? (x) : (y))

// Read all bytes of the data runs contained in a DATA attribute within an MFT header.
static size_t read_nonresident_data(struct ntfs_volume *vol, char *buffer, struct attribute_nonresident *attr, size_t max, struct block_device *device) {
    size_t amt_to_read = MIN(max, attr->content_actual_size);
    off_t amt_written = 0;
    char *runlist = (char*)attr + attr->offset_to_runlist;
    size_t i = 0;
    uint8_t field_sizes = runlist[i];

    afs_assert(attr->start_vcn_runlist == 0, stop,
               "Artifice has not implemented support for non-0 starting VCNs.");

    while (field_sizes) {
        /* NTFS runs use variable-sized fields. The size of the offset and lengths of the runs
         * are determined by which bits are set in the first and second nibbles of `runlist`.
         * The high nibble tells us how many bytes large the offset is,
         * and the low nibble tells us how many bytes large the length for this run is.
         */
        uint8_t offset_size_bytes = (field_sizes >> 4) & 0xF;
        uint8_t offset_size_bits = offset_size_bytes << 3;
        uint64_t offset_size_mask = ((1UL << offset_size_bits) - 1);

        uint8_t length_size_bytes = (field_sizes) & 0xF;
        uint8_t length_size_bits = length_size_bytes << 3;
        uint64_t length_size_mask = ((1UL << length_size_bits) - 1);

        int64_t length, offset, cluster;

        afs_debug("Field size octet: %d", field_sizes);
        afs_debug("Offset size, length size %d %d", offset_size_bits, length_size_bits);

        // Use int64_t because offset and length fields may be up to 8 bytes large
        length = (*(int64_t*)(runlist + i + 1)) & (length_size_mask);
        offset = (*(int64_t*)(runlist + i + 1 + length_size_bytes)) & (offset_size_mask);

        afs_debug("Got offset and length: %lld %lld", offset, length);

        afs_debug("Attempting to read %lu bytes", amt_to_read);

        // Move through each logical cluster number for this run, reading the data
        // from each cluster.
        for (cluster = offset; cluster < offset + length; cluster++) {
            read_ntfs_cluster(buffer + amt_written, vol, device, cluster, true);
            amt_written += vol->bytes_per_cluster;
            if (amt_written >= amt_to_read) {
                afs_debug("Stopping runlist reading %lu >= %lu at %lld/%lld", amt_written, amt_to_read, cluster, offset + length);
                goto stop;
            }
        }

        // Move to the next field size octet.
        i += 1 + length_size_bytes + offset_size_bytes;
        field_sizes = runlist[i];
    }
stop:
    return amt_written;
}

// Read all data from a particular file.
// This is only used for reading the $Bitmap metafile.
static size_t read_file(struct ntfs_volume *vol, char *buffer, ssize_t max_bytes, struct mft_header *header, struct block_device *device) {
    char *record = (char*)header;
    int record_offset = header->offset_to_first_attribute;
    off_t buffer_offset = 0;

    while (record_offset < vol->mft_record_size) {
        struct attribute_header *attr_header = (struct attribute_header*)(record + record_offset);
        uint32_t type_id = le32_to_cpu(attr_header->type_id);
        afs_debug("Got attribute with type: %x", type_id);
        switch(type_id) {
            case $DATA:
                // Read this data run into `buffer`.
                afs_debug("Found data in NTFS cluster");
                if (!attr_header->nonresident_flag) {
                    // Data is resident in this MFT entry.
                    // The data is stored immediately after the MFT header.
                    ssize_t amount = MIN(max_bytes - buffer_offset, attr_header->length);
                    if (amount < 0) {
                        afs_debug("Preempting any further reads");
                        goto done_with_attributes;
                    }
                    memcpy(buffer + buffer_offset, (char*)(attr_header + 1), amount);
                    buffer_offset += amount;
                } else {
                    afs_debug("Found non-resident data");
                    // Data is not resident, so we need to lookup the logical clusters
                    // in each of the contained data runs within this data attribute.
                    buffer_offset += read_nonresident_data(vol, buffer + buffer_offset, (struct attribute_nonresident*)attr_header, max_bytes - buffer_offset, device);
                }
                break;
            case $ATTRIBUTE_LIST:
                // The Attribute List attribute is not supported.
                // It may define additional data sections that will not be detected
                // by dm-afs.
                // Attribute List should never be required for $Bitmap.
                // TODO: Add support for Attribute List.
                afs_debug("$ATTRIBUTE_LIST attribute found. This is not yet supported, "
                          "so the correctness of the output from this function is "
                          "not guaranteed.");
                break;
            case ATTRS_DONE:
                goto done_with_attributes;
            default:
                break;
        }

        // If the attribute length is 0, either we have read
        // bad data from the disk, or we are not reading an attribute header.
        if (attr_header->length == 0) {
            afs_debug("BUG: Got attr header length of 0");
            break;
        }
        record_offset += attr_header->length;
    }
done_with_attributes:
    return buffer_offset;
}

#undef MIN

static int
extract_bitmap(struct ntfs_volume *vol, struct block_device *device) {
    ssize_t max = vol->cluster_count / 8;
    uint8_t *bitmap = vmalloc(max);
    size_t i, read, total_unused_clusters, empty_block_idx;
    uint32_t *empty_blocks;

    afs_debug("Got maximum bitmap size of %ld", max);

    // The NTFS $Bitmap clusters start at 0, so we will also start at 0.
    vol->data_start_off = 0;
    read = read_file(vol, bitmap, max, vol->metafiles[$bitmap], device);
    if (!read) {
        afs_debug("Didn't read anything from $Bitmap");
        vfree(bitmap);
        return 1;
    }

    total_unused_clusters = 0;
    afs_debug("Read %ld entries from $Bitmap", read);
    for (i = 0; i < read; ++i) {
        // hweight is the number of bits set
        total_unused_clusters += 8 - hweight8(bitmap[i]);
    }

    afs_debug("Total number of unused clusters %ld", total_unused_clusters);

    empty_block_idx = 0;
    empty_blocks = vmalloc(sizeof(uint32_t) * total_unused_clusters * vol->afs_blocks_per_cluster);
    if (!empty_blocks) {
        afs_debug("Could not allocate empty block list");
        vfree(bitmap);
        return 1;
    }

    for (i = 0; i < read; ++i) {
        unsigned short bpos;
        for (bpos = 0; bpos < 8; ++bpos) {
            char bit = (bitmap[i] >> bpos) & 0x1;
            int block_num;

            // If cluster is in use, we just ignore it.
            if (bit)
                continue;

            for (block_num = 0; block_num < vol->afs_blocks_per_cluster; ++block_num) {
                uint32_t block = (i * 8 + bpos) * vol->afs_blocks_per_cluster + block_num;
                // If there are problems adding empty blocks to `empty_blocks`,
                // you may wish to uncomment the following line.
                // Be careful, this will print A LOT!
                //
                // afs_debug("Adding %u (%ld/%ld)", block, empty_block_idx,
                //           total_unused_clusters * vol->afs_blocks_per_cluster);

                // Verify that we are not about to overrun `empty_blocks`.
                if (empty_block_idx >= total_unused_clusters * vol->afs_blocks_per_cluster) {
                    afs_debug("Incorrect precalculation of free clusters. Stopping early...");
                    goto stop;
                }

                empty_blocks[empty_block_idx++] = block;
            }
        }
    }

    vfree(bitmap);
    afs_debug("NTFS bitmap successfully read");
    vol->empty_blocks = empty_blocks;
    vol->num_empty_afs_blocks = total_unused_clusters;
    // `empty_blocks` is not freed as it is passed back to dm_afs.
    return 0;

stop:
    vfree(bitmap);
    vfree(empty_blocks);
    return 1;
}

static int
ntfs_map(struct ntfs_volume *vol, void *data, struct block_device *device)
{
    struct mft_header *mft;
    int mft_number, status, ret;

    // Allocate space for metafiles
    char *records = vmalloc(12 * vol->mft_record_size);
    afs_action(!IS_ERR(records), status = PTR_ERR(records),
        could_not_allocate, "Couldn't allocate space for metafile records [%d]", status);

    // Read MFT metafiles
    for (mft_number = 0; mft_number < 12; mft_number += vol->mft_records_per_cluster) {
        int i;
        char *record_cluster = records + mft_number * vol->mft_record_size;
        read_mft_records(record_cluster, vol, device, mft_number, /*used_vmalloc=*/true);
        for (i = 0; i < vol->mft_records_per_cluster; ++i) {
            afs_debug("Reading MFT entry number (base: %d) (actual: %d)",
                      mft_number, mft_number + i);
            vol->metafiles[mft_number + i] =
                (struct mft_header *)(record_cluster + (i * vol->mft_record_size));
        }
    }

    // Verify that the size of each record from the NTFS boot sector
    // matches what is reported by the $MFT metafile.
    mft = vol->metafiles[$mft];
    afs_assert(mft->allocated_size_of_record == vol->mft_record_size,
            ntfs_map_invalid, "MFT sizes do not match: %d != %d",
            mft->allocated_size_of_record, vol->mft_record_size);

    ret = extract_bitmap(vol, device);
    if (ret) {
        goto ntfs_map_invalid;
    }

    vfree(records);
    return 0;

ntfs_map_invalid:
    vfree(records);
could_not_allocate:
    return 1;
}

/**
 * Detect the presence of an NTFS file system
 * on 'device'.
 *
 * @data    The first 4KB of the device.
 * @fs      The file system information to be filled in.
 * @return  boolean.
 */
bool
afs_ntfs_detect(const void *data, struct block_device *device, struct afs_passive_fs *fs)
{
    struct ntfs_volume vol;
    int ret;

    afs_debug("Attempting to detect NTFS filesystem");

    // Read the boot sector.
    ret = read_boot_sector(&vol, data);
    if (ret) {
        afs_debug("Failed to read boot sector");
        goto vol_err;
    }

    // If this is an NTFS volume, find the sectors that Artifice can use.
    ret = ntfs_map(&vol, (void *)data, device);
    if (ret) {
        afs_debug("Failed to map filesystem");
        goto vol_err;
    }

    if (fs) {
        fs->total_blocks = vol.num_afs_blocks;
        fs->sectors_per_block = AFS_BLOCK_SIZE / AFS_SECTOR_SIZE;
        fs->block_list = vol.empty_blocks;
        fs->list_len = vol.num_empty_afs_blocks;
        fs->data_start_off = vol.data_start_off;
        return true;
    }

vol_err:
    return false;
}
