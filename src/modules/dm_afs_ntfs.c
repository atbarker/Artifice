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

#define MFT_HEADER_SIZE 48

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
    uint8_t  sectors_per_cluster;
    uint64_t bytes_per_cluster;

    uint64_t mft_cluster;
    uint64_t mft_mirror_cluster;

    uint32_t mft_record_size;

    uint8_t  afs_blocks_per_cluster;

    uint32_t num_data_clusters;
    uint8_t  sectors_per_afs_block;
    uint32_t *empty_clusters;
    uint32_t num_empty_clusters;
    off_t    data_start_off;
};

static const char ntfs_magic[] = "NTFS    ";

bool is_ntfs_magic_value(char *oem_name) {
    return strncmp(oem_name, ntfs_magic, 8) == 0;
}

static int
read_boot_sector(struct ntfs_volume *vol, const void *data)
{
    static char oem_name[9] = { 0 };
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

    if (boot_sector->ebpb.clusters_per_record > 0) {
        afs_debug("Got positive clusters per record");
        vol->mft_record_size = vol->bytes_per_cluster * boot_sector->ebpb.clusters_per_record;
    } else {
        afs_debug("Got negative clusters per record");
        vol->mft_record_size =  1 << (-boot_sector->ebpb.clusters_per_record);
    }

    if (vol->bytes_per_cluster % AFS_BLOCK_SIZE) {
        afs_assert(false, boot_sector_invalid,
                "NTFS volume incompatible with Artifice: invalid block size [%llu]",
                vol->bytes_per_cluster);
    }

    vol->sectors_per_afs_block = vol->bytes_per_cluster / AFS_SECTOR_SIZE;
    vol->afs_blocks_per_cluster = vol->bytes_per_cluster / AFS_BLOCK_SIZE;

    afs_debug("Found valid boot sector for NTFS volume of size: %lld bytes",
            vol->sector_count * vol->bytes_per_sector);

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

#define UNUSED         0x00
#define STANDARD_INFO  0x10
#define ATTR_LIST      0x20
#define FILENAME       0x30
#define ATTRS_DONE     0xFFFFFFFF

void parse_mft_record(char *record, struct ntfs_volume *vol) {
    struct mft_header *header = (struct mft_header*)record;
    int record_offset = header->offset_to_first_attribute;
    afs_debug("Expecting first attribute id to be: %d", header->next_attr_id);
    while (record_offset < vol->mft_record_size) {
        struct attribute_header *attr_header = (struct attribute_header*)(record + record_offset);
        uint32_t type_id = le32_to_cpu(attr_header->type_id);
        afs_debug("Got attribute with type: %x", type_id);
        switch(type_id) {
            case UNUSED:
                break;
            case STANDARD_INFO:
                break;
            case ATTR_LIST:
                break;
            case FILENAME:
                print_filename(attr_header);
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
    return;
}

#undef UNUSED
#undef STANDARD_INFO
#undef ATTR_LIST
#undef FILENAME
#undef ATTR_DONE

static int
read_mft_record(char *record, struct ntfs_volume *vol, struct block_device *device, uint32_t number) {
    int mft_offset;
    int ret;
    uint64_t cluster = vol->mft_cluster + number * vol->mft_record_size;
    for (mft_offset = 0; mft_offset < vol->mft_record_size; mft_offset += vol->bytes_per_cluster) {
        ret = read_ntfs_cluster(record + mft_offset, vol, device, cluster + mft_offset,
                /*used_vmalloc=*/true);
        if (ret)
            return ret;
    }
    return 0;
}

static int
ntfs_map(struct ntfs_volume *vol, void *data, struct block_device *device)
{
    struct mft_header *header;
    int mft_number;
    // Allocate space for MFT record
    char *record = vmalloc(vol->mft_record_size);
    read_mft_record(record, vol, device, /*number=*/0);

    header = (struct mft_header*)record;

    afs_assert(header->allocated_size_of_record == vol->mft_record_size,
            ntfs_map_invalid, "MFT sizes do not match: %d != %d",
            header->allocated_size_of_record, vol->mft_record_size);

    parse_mft_record(record, vol);

    /*
    for (mft_number = 1; mft_number < 12; ++mft_number) {
        read_mft_record(record, vol, device, mft_number);
        parse_mft_record(record, vol);
    }
    */

ntfs_map_invalid:
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

    static_assert(sizeof(struct mft_header) == MFT_HEADER_SIZE,
            "MFT header has invalid size");

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

    //TODO Really should just store the whole volume struct but oh well
    if (fs) {
        fs->total_blocks = vol.num_data_clusters;
        fs->sectors_per_block = vol.sectors_per_afs_block;
        fs->block_list = vol.empty_clusters;
        fs->list_len = vol.num_empty_clusters;
        fs->data_start_off = vol.data_start_off; // Data start in sectors, blocks are relative to this.
        return true;
    }

vol_err:
    return false;
}

#undef MFT_HEADER_SIZE
