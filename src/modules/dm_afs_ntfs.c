/*
 * Author: Fill in.
 * Copyright: Fill in.
 */
#include <dm_afs.h>
#include <dm_afs_modules.h>

#include <linux/kern_levels.h>

#include <stddef.h>

/**
 * Detect the presence of an NTFS file system
 * on 'device'.
 *
 * @data    The first 4KB of the device.
 * @fs      The file system information to be filled in.
 * @return  boolean.
 */

// All integers in BPB and EBPB are stored on disk as little endian.
struct ntfs_bpb {
    uint16_t bytes_per_sector;
    int8_t   sectors_per_cluster;
    uint16_t reserved_sector_count; // always 9
    int8_t   table_count;           // unused, always 0
    uint16_t root_entry_count;      // unused, always 0
    uint16_t sector_count;          // unused, always 0
    int8_t   media_type;
    uint16_t sectors_per_table;     // unused, always 0
    uint16_t sectors_per_track;
    uint16_t heads;
    uint32_t hidden_sector_count;
    uint32_t sector_count_32;       // unused
    uint32_t reserved;              // unused
} __attribute__((packed));

struct ntfs_ebpb {
    uint64_t sector_count_64;
    uint64_t master_file_table_cluster;
    uint64_t master_file_table_mirror_cluster;
    int8_t   clusters_per_record;   // positive: clus per rec, negative: bytes per rec
    int8_t   reserved1[3];
    int8_t   clusters_per_index_buffer; // pos: clus per IB, neg: bytes per IB
    int8_t   reserved2[3];
    uint64_t serial_number;
    uint32_t checksum;
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
    uint16_t offset_to_update_seq;
    uint16_t entries_in_fixup_array;
    uint64_t logfile_sequence_number;
    uint16_t record_usage_count;
    uint16_t hardlink_count;
    uint16_t offset_to_first_attribute;
    uint16_t flags;
    uint32_t size_of_record;
    uint32_t allocated_size_of_record;
    uint64_t base_file_record;
    uint16_t next_attr_id;
    uint16_t unused;
    uint32_t mft_record_number;
} __attribute__((packed));

struct attribute_header {
    uint32_t type_id;
    uint32_t length;
    uint8_t nonresident_flag;
    uint8_t name_length;
    uint16_t offset_to_name;
    uint16_t flags;
} __attribute((packed));

struct attribute_resident {
    struct attribute_header header;
    uint16_t attribute_id;
    uint32_t content_length;
    uint16_t offset_to_content;
} __attribute((packed));

struct attribute_nonresident {
    struct attribute_header header;
    uint16_t attribute_id;
    uint64_t start_vcn_runlist;
    uint64_t end_vcn_runlist;
    uint16_t offset_to_runlist;
    uint16_t compression_unit_size;
    uint32_t reserved;
    uint64_t content_allocated_size;
    uint64_t content_actual_size;
    uint64_t content_initialized_size;
} __attribute((packed));

// All data endianness corrected for this CPU
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
    union {
        struct {
            struct mft_header *mft;
            struct mft_header *mft_mirr;
            struct mft_header *log_file;
            struct mft_header *volume;
            struct mft_header *attr_def;
            struct mft_header *root_dir;
            struct mft_header *bitmap;
            struct mft_header *boot;
            struct mft_header *bad_clus;
            struct mft_header *secure;
            struct mft_header *up_case;
            struct mft_header *extend;
        } named;
        struct mft_header *index[12];
    } metafiles;
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

    // afs_assert(reserved_sector_count == 9, boot_sector_invalid,
    //         "Reserved sector count: %d != 9", reserved_sector_count);

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

    if (boot_sector->ebpb.clusters_per_record > 0) {
        afs_debug("Got positive clusters per record");
        vol->mft_record_size = vol->bytes_per_cluster * boot_sector->ebpb.clusters_per_record;
    } else {
        afs_debug("Got negative clusters per record");
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

#define MIN(x, y) (x < y ? (x) : (y))

void print_filename(struct attribute_header *attr_header) {
    char buffer[64] = {0};

    afs_debug("About to print filename");

    if (attr_header->nonresident_flag) {
        // struct attribute_nonresident *attr = attr_header;
        afs_debug("Non-resident: ignoring...");
    } else {
        struct attribute_resident *attr = (struct attribute_resident*)attr_header;
        char *attr_start = (char*)attr;
        char *filename = attr_start + attr->offset_to_content;
        memcpy(buffer, filename, MIN(64, attr->content_length));
        afs_debug("Got filename %ls", (wchar_t*)buffer);
        afs_debug("Got attr length %u", attr->content_length);
    }
}

const uint32_t ATTRS_DONE = 0xFFFFFFFF;

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
read_mft_records(char *record, struct ntfs_volume *vol, struct block_device *device, uint32_t number) {
    int mft_offset;
    int ret;
    afs_assert(number % vol->mft_records_per_cluster == 0,
            invalid_record, "Asked for MFT record that is not aligned: %d", number);
    uint64_t cluster = vol->mft_cluster + number * vol->mft_records_per_cluster;
    for (mft_offset = 0; mft_offset < vol->mft_record_size; mft_offset += vol->bytes_per_cluster) {
        ret = read_ntfs_cluster(record + mft_offset, vol, device, cluster + mft_offset,
                /*used_vmalloc=*/true);
        if (ret)
            return ret;
    }
    return 0;

invalid_record:
    return 1;
}

static size_t read_data(struct ntfs_volume *vol, char *buffer, size_t max_bytes, struct mft_header *header) {
    char *record = (char*)header;
    int record_offset = header->offset_to_first_attribute;
    off_t buffer_offset = 0;

    while (record_offset < vol->mft_record_size) {
        struct attribute_header *attr_header = (struct attribute_header*)(record + record_offset);
        uint32_t type_id = le32_to_cpu(attr_header->type_id);
        size_t amount = MIN(max_bytes - buffer_offset, attr_header->length);
        afs_debug("Got attribute with type: %x", type_id);
        switch(type_id) {
            case $DATA:
                memcpy(buffer + buffer_offset, (char*)(attr_header + 1), amount);
                break;
            case $ATTRIBUTE_LIST:
                afs_debug("Found $ATTRIBUTE_LIST so cannot guarantee correctness");
                break;
            case ATTRS_DONE:
                goto done_with_attributes;
            default:
                break;
        }

        if (attr_header->length == 0) {
            afs_debug("BUG: Got attr header length of 0");
            break;
        }
        record_offset += attr_header->length;
    }
done_with_attributes:
    return buffer_offset;
}

static void
extract_bitmap(struct ntfs_volume *vol) {
    size_t max = vol->cluster_count / 8;
    uint8_t *bitmap = vmalloc(max);
    vol->data_start_off = vol->mft_cluster;
    size_t read = read_data(vol, bitmap, max, vol->metafiles.named.bitmap);
    if (!read) {
        afs_debug("Didn't read anything from the bitmap");
        vfree(bitmap);
        return;
    }

    size_t i = 0;
    unsigned short bpos = 0;
    int block_num = 0;

    size_t total_unused_clusters = 0;
    afs_debug("Read %ld entries from bitmap", read);
    for (i = 0; i < read; ++i) {
        // hweight is the number of bits set
        total_unused_clusters = 8 - hweight8(bitmap[i]);
    }

    afs_debug("Total number of unused clusters %ld", total_unused_clusters);

    size_t empty_block_idx = 0;
    uint32_t *empty_blocks = vmalloc(total_unused_clusters * vol->afs_blocks_per_cluster);
    for (i = 0; i < read; ++i) {
        for (bpos = 0; bpos < 8; ++bpos) {
            char bit = (bitmap[i] >> bpos) & 0x1;
            if (!bit) {
                for (block_num = 0; i < vol->afs_blocks_per_cluster; ++block_num) {
                    empty_blocks[empty_block_idx++] = (read * 8 + bpos) * vol->afs_blocks_per_cluster + block_num;
                }
            }
        }
    }
    vol->empty_blocks = empty_blocks;
    vol->num_empty_afs_blocks = total_unused_clusters;
}

static int
ntfs_map(struct ntfs_volume *vol, void *data, struct block_device *device)
{
    struct mft_header *mft;
    int mft_number;
    int i;
    int status;

    // Allocate space for metafiles
    char *records = vmalloc(12 * vol->mft_record_size);
    afs_action(!IS_ERR(records), status = PTR_ERR(records),
        could_not_allocate, "couldn't allocate space for metafile records [%d]", status);

    // Read MFT metafiles
    for (mft_number = 0; mft_number < 12; mft_number += vol->mft_records_per_cluster) {
        read_mft_records(records + mft_number * vol->mft_record_size, vol, device, mft_number);
        for (i = 0; i < vol->mft_records_per_cluster; ++i) {
            afs_debug("Reading MFT entry number %d", mft_number + i);
            if (mft_number + i >= 12) break;
            vol->metafiles.index[mft_number + i] =
                (struct mft_header *)(records + (mft_number + i) * vol->mft_record_size);
        }
    }

    mft = vol->metafiles.named.mft;
    afs_assert(mft->allocated_size_of_record == vol->mft_record_size,
            ntfs_map_invalid, "MFT sizes do not match: %d != %d",
            mft->allocated_size_of_record, vol->mft_record_size);

    extract_bitmap(vol);

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

    ret = ntfs_map(&vol, (void *)data, device);
    if (ret) {
        afs_debug("Failed to map filesystem");
        goto vol_err;
    }

    // vol->data_start_off = (off_t)((vol->tables * vol->sec_fat) + vol->reserved);

    if (fs) {
        fs->total_blocks = vol.num_afs_blocks;
        fs->sectors_per_block = AFS_BLOCK_SIZE / AFS_SECTOR_SIZE;
        fs->block_list = vol.empty_blocks;
        fs->list_len = vol.num_empty_afs_blocks;
        fs->data_start_off = vol.data_start_off; // Data start in sectors, blocks are relative to this.
        return true;
    }

vol_err:
    return false;
}
