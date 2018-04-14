/*
 * Library calls for support of the FAT32 filesystem
 * for matryoshka.
 *
 * It only supports FAT32, no FAT16 or FAT12.
 * 
 * Author: Austen Barker <atbarker@ucsc.edu>
 * Copyright: UC Santa Cruz SSRC
 */
#include <dm_mks_lib.h>
#include <dm_mks_utilities.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/slab.h>

//This is hell incarnate, all the information about a FAT volume
struct fat_volume {

	void *fat_map;              // FAT mapped into memory.
	u32 *empty_clusters;        // list of empty clusters (blocks).
	u32 num_data_clusters;      // Number of data cluster on the disk.
	off_t data_start_off;       // byte offset for the second cluster.
	size_t num_alloc_files;     // number of allocated files.
	size_t max_allocated_files; // number of allocatable files.
	char oem_name[8+1];         // boot sector information.
        u32 num_empty_clusters;

	//data from the DOS 2.0 parameter block
	u16 bytes_sector;           // bytes in a sector.
	u16 sector_order;           // sector order.
	u8 sec_cluster;             // sectors in cluster.
	u8 sec_cluster_order;       // sector cluster order.
	u16 cluster_order;          // cluster order.
	u16 reserved;               // reserved sectors.
	u8 tables;                  // number of fat tables.
	u16 root_entries;           // max root entries.
	u8 media_desc;              // media description.
	
	u32 total_sec;              // total number of sectors.
	u32 sec_fat;                // sectors per FAT.

	//Data from DOS 3.0 block
	u16 sec_track;              // sectors per track.
	u16 num_heads;              // number of heads.
	u32 hidden_sec;             // number of hidden sectors.

	//FAT32 extended;
	u16 driv_desc;              // drive description.
	u16 version;                // drive version.
	u32 root_dir_start;         // start of the root directory.
	u16 fs_info_sec;            // info sector offset.
	u16 alt_boot_sec;           // alternate boot sector.

	//nonFAT32 extended
	u8 phys_driv_num;           // physical drive number.
	u8 ext_boot_sig;            // boot signature.
	u32 vol_id;                 // volume id.
	char volume_label[11+1];    // volume label.
	char fs_type[8 + 1];        // FS type.

};

/*Extended BIOS parameter block for FAT32 */
struct fat32_ebpb {

	__le32 sec_fat;               //sectors per fat
	__le16 drive_desc;            //drive description
	__le16 version;               //version
	__le32 root_start_clust;      //starting cluster for the root dir.
	__le16 fs_info_sec;           //info sector
	__le16 alt_boot_sec;          //alternate boot sector
	u8 reserved[12];

} __attribute__((packed));

struct nonfat32_ebpb {
	u8 physical_drive_num;
	u8 reserved;
	u8 extended_boot_sig;
	__le32 volume_id;
	char volume_label[11];
	char fs_type[8];
} __attribute__((packed));

/*FAT32 boot sector, exactly how it appears on your disk*/
struct fat_boot_sector {
        
	u8 jump_insn[3];            //bootloader jump inst. (3 bytes)

	//standard info (8 bytes)
	char oem_name[8];

	//dos 2.0 parameter block (13 bytes)
	__le16 bytes_sec;             //bytes per sec
	u8 sec_cluster;             //sectors per cluster
	__le16 res_sec;               //reserved sectors
	u8 num_tables;		    //number of allocation tables
	__le16 max_root_ent;          //maximum root entries
	__le16 total_sectors;         //total sectors on disk
        u8 media_desc;              //media descriptor
	__le16 sec_fat;               //sectors per fat

	//dos 3.31 parameter block (12 bytes)
	__le16 sec_track;             //sectors on each track
	__le16 num_heads;             //number of heads
	__le32 hidden_sec;            //number of hidden sectors
	__le32 total_sec_32;          //total sectors (32 bit number)

	//bios parameter block (FAT32, no 16 or 12 support)
	//TODO: clean this up
	union __attribute((packed)) {
		struct __attribute__((packed)){
			struct fat32_ebpb fat32_ebpb;
			struct nonfat32_ebpb nonfat32_ebpb;
		} fat32;
		struct nonfat32_ebpb nonfat32_ebpb;
	} ebpb;
} __attribute__((packed));

/**
 *Reads in boot parameter block as seen in DOS 2.0.
 *
 *
 */
int 
fat_read_dos_2_0_bpb(struct fat_volume *vol, const struct fat_boot_sector *boot_sec)
{
	vol->bytes_sector = le16_to_cpu(boot_sec->bytes_sec);
	vol->sector_order = bsr(vol->bytes_sector);
	if(!is_power_of_2(vol->bytes_sector) || vol->sector_order < 5 || vol->sector_order > 12){
		//error out of this function
		goto out_invalid;
	}
	vol->sec_cluster = boot_sec->sec_cluster;
	vol->sec_cluster_order = bsr(vol->sec_cluster);
	if (!is_power_of_2(vol->sec_cluster) || vol->sec_cluster_order > 7){
		//error out
		goto out_invalid;
	}
	vol->cluster_order = vol->sector_order + vol->sec_cluster_order;

	vol->reserved = le16_to_cpu(boot_sec->res_sec);

	vol->tables = boot_sec->num_tables;
	if(vol->tables != 1 && vol->tables != 2){
		//error out, number of tables
		goto out_invalid;
	}	

	vol->root_entries = le16_to_cpu(boot_sec->max_root_ent);
	if(vol->root_entries == 0){
		//then this is a FAT32 volume
	}else{
		
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
 *@params
 *    struct fat_volume, place to put the data
 *    struct fat_boot_sector, the boot sector copy
 *@return
 *    status int
 */
int 
fat_read_dos_3_31_bpb(struct fat_volume *vol, const struct fat_boot_sector *boot_sec)
{
	vol->sec_track = le16_to_cpu(boot_sec->sec_track);
	vol->num_heads = le16_to_cpu(boot_sec->num_heads);
	vol->hidden_sec = le32_to_cpu(boot_sec->hidden_sec);
	if( vol->total_sec == 0){
		vol->total_sec = le32_to_cpu(boot_sec->total_sec_32);
	}else {
		//16 bit sectors
	}
	return 0;
}

/**
 *Non fat32 extended boot parameter block helper
 *
 */

int 
fat_read_nonfat32_ebpb(struct fat_volume *vol, const struct nonfat32_ebpb *ebpb)
{
	vol->phys_driv_num = ebpb->physical_drive_num;
	vol->ext_boot_sig = ebpb->extended_boot_sig;
	vol->vol_id = le32_to_cpu(ebpb->volume_id);
	memcpy(vol->volume_label, ebpb->volume_label, sizeof(ebpb->volume_label));
	memcpy(vol->fs_type, ebpb->fs_type, sizeof(ebpb->fs_type));
	//debug here
	return 0;
}

/**
 *FAT32 extended boot parameter block helper.
 *
 */

int 
fat_read_fat32_ebpb(struct fat_volume *vol, const struct fat32_ebpb *ebpb)
{	
	if (le32_to_cpu(ebpb->sec_fat) != 0){
		vol->sec_fat = le32_to_cpu(ebpb->sec_fat);
		if((((size_t)vol->sec_fat << vol->sector_order) >> vol->sector_order) != (size_t)vol->sec_fat){
			//error out
			goto out_invalid;
		}

	}
	vol->driv_desc = le16_to_cpu(ebpb->drive_desc);
	vol->version = le16_to_cpu(ebpb->version);
	if(vol->version != 0){
		//error
		goto out_invalid;
	}
	vol->root_dir_start = le32_to_cpu(ebpb->root_start_clust);
	if(vol->root_dir_start == 0){
		//invalid position for starting cluster
		goto out_invalid;
	}
	vol->fs_info_sec = le16_to_cpu(ebpb->fs_info_sec);
	vol->alt_boot_sec = le16_to_cpu(ebpb->alt_boot_sec);
	if(vol->fs_info_sec == 0xffff){
		vol->fs_info_sec = 0;
	}
	if(vol->fs_info_sec != 0 && vol->sector_order < 9){
		goto out_invalid;
	}
	return 0;
out_invalid:
	return -1;

}

/**
 *Reads in parameters from the FAT superblock.
 *If this fails the file system is not FAT or
 *is corrupted.
 *
 *@params
 *	struct fat_volume, FAT volume summary
 *	void *data, first 4096 bytes in device
 *@return
 *	status int
 */

int 
read_boot_sector(struct fat_volume *vol, const void *data)
{
	//u8 buf[512];
	struct fat_boot_sector *boot_sec = kmalloc(512, GFP_KERNEL);
	int ret;
	u32 num_data_sectors;

	//copy first 512 bytes into the fat_boot_sector
	memcpy(boot_sec, data, 512);

	memcpy(vol->oem_name, boot_sec->oem_name, sizeof(boot_sec->oem_name));

	//TODO remove trailing spaces from vol->oem_name
	//to be honest I don't really need it.
	
	ret = fat_read_dos_2_0_bpb(vol, boot_sec);
	if(ret){
		return ret;
	}
	ret = fat_read_dos_3_31_bpb(vol, boot_sec);
	if(ret){
		return ret;
	}

        ret = fat_read_fat32_ebpb(vol, &boot_sec->ebpb.fat32.fat32_ebpb);
	if(ret){
		return ret;
	}
	ret = fat_read_nonfat32_ebpb(vol, &boot_sec->ebpb.nonfat32_ebpb);

	num_data_sectors = vol->total_sec - vol->reserved - ((vol->root_entries << 5) >> vol->sector_order);
	mks_debug("Number of data sectors: %u\n", num_data_sectors);
	vol->num_data_clusters = num_data_sectors >> vol->sec_cluster_order;
	return 0;
}

/**
 *Calculates the aligned FAT offset and size.
 *Maps the FAT to memory and finds unallocated clusters.
 *
 *@params
 *    struct fat_volume, summary of the fat device
 *    void *data, superblock contents
 *    struct block_device, block device being read from
 *
 *@return int status
 *    return ==0 success
 *    return !=0 fail
 */

int 
fat_map(struct fat_volume *vol, void *data, struct block_device *device)
{	
	size_t fat_size_bytes;
	size_t fat_aligned_size_bytes;
	off_t fat_offset;
	off_t fat_aligned_offset;
	u32 *p;
	u32 *empty_clusters;
	int i;
	int status;
	int cluster_number;
	sector_t start_sector;
	void *fat_data;
	struct page *page = NULL;
	struct mks_io fatio;
	enum mks_io_flags flags;
	u32 page_size;
	u32 page_number;

	page_size = 1 << PAGE_SHIFT;
	fat_offset = (off_t)vol->reserved << vol->sector_order;
	fat_aligned_offset = fat_offset & ~(page_size -1);
	fat_size_bytes = (size_t)vol->sec_fat << vol->sector_order;
	fat_aligned_size_bytes = fat_size_bytes + (fat_offset - fat_aligned_offset);
	page_number = fat_aligned_size_bytes/512;

	mks_debug("FAT aligned size in bytes: %zu \n", fat_aligned_size_bytes);
	mks_debug("FAT aligned offset: %d \n", (int)fat_aligned_offset);
	mks_debug("FAT size in pages: %u \n", page_number);
        //mks_debug("Reserved %u\n", vol->reserved);

	//start_sector = (sector_t)(fat_aligned_offset/512);
        start_sector = 0;
	
	if(fat_aligned_offset + fat_aligned_size_bytes > 4096){
		mks_debug("Read more data to map the FAT\n");
		page = alloc_pages(GFP_KERNEL, (unsigned int)bsr(page_number));
                fat_data = page_address(page);
		fatio.bdev = device;
                fatio.io_page = page;
		fatio.io_sector = fat_aligned_offset/512;
		fatio.io_size = fat_aligned_size_bytes;
		flags = MKS_IO_READ;
		status = mks_blkdev_io(&fatio, flags);
		mks_debug("FAT read successfully.\n");	
	}
	
	vol->fat_map = data;

	p = (int*)data;
        mks_debug("p: %p\n", p);
	cluster_number = 0;
	empty_clusters = kmalloc(fat_size_bytes, GFP_KERNEL);
        memset((void*)empty_clusters, 0, fat_size_bytes);
	//uint8_t *cluster_contents = kmalloc(1, GFP_KERNEL);
	for (i=0; i<100; i++){
                mks_debug("block %d: %d\n",i, p[i]);
		if(p[i] == 0){
                        //mks_debug("block {%d} is empty", i);
			empty_clusters[cluster_number] = i;
                        //mks_debug("empty block %d\n", empty_clusters[cluster_number]);
                        cluster_number++;
		}
	}
	vol->empty_clusters = empty_clusters;
        vol->num_empty_clusters = cluster_number;
	return 0;
}

/**
 *Parses a FAT32 (or DOS compatible) file system.
 *Returns a struct containing a map of the free space on the disk
 *and basic metadata.
 *
 *@params
 *    void *data, first 4096 bytes in the block device
 *    struct block_device *device, block device we are reading from
 *
 *@return
 *    struct fs_data *, contains generalized data about the fs.
 */

struct fs_data * 
mks_fat32_parse(void *data, struct block_device *device)
{
	struct fat_volume *vol = NULL;
	struct fs_data *parameters = NULL;
	int ret;

	//allocate stuff;
	vol = kmalloc(sizeof(struct fat_volume), GFP_KERNEL);
	parameters = kmalloc(sizeof(struct fs_data), GFP_KERNEL);
	
	//read the boot sector
	ret = read_boot_sector(vol, data);
	if(ret){
		mks_debug("Failed to read boot sector");
		return NULL;
	}

	ret = fat_map(vol, data, device);
	if(ret){
		mks_debug("Failed to map FAT");
		return NULL;
	}

	//set the data start offset
	vol->data_start_off = (off_t)(vol->tables * vol->sec_fat + vol->reserved + 
				      (vol->root_entries >> (vol->sector_order - 5))) 
					<< vol->sector_order;

        vol->data_start_off = (off_t)(vol->tables * vol->sec_fat + vol->reserved);
	parameters->num_blocks = vol->num_data_clusters;
	parameters->bytes_sec = vol->bytes_sector;
	parameters->sec_block = vol->sec_cluster;
	parameters->bytes_block = vol->bytes_sector * vol->sec_cluster;
	parameters->data_start_off = (u32)vol->data_start_off;
	parameters->empty_block_offsets = vol->empty_clusters;
        parameters->num_empty_blocks = vol->num_empty_clusters;

	return parameters;
}

/**
 * Detect presence of a FAT32 filesystem. This is done by
 * sifting through the binary data and looking for FAT32
 * headers.
 * 
 * @param   data    The data to look into.
 * 
 * @return  mks_boolean
 *  DM_MKS_TRUE     data is formatted as FAT32.
 *  DM_MKS_FALSE    data is not formatted as FAT32.
 */

mks_boolean_t 
mks_fat32_detect(const void *data, struct mks_fs_context *fs, struct block_device *device)
{
    struct fs_data *fat = mks_fat32_parse((void *)data, device);
    if(fs) {
        fs->total_blocks = fat->num_blocks;
        fs->sectors_per_block = fat->sec_block;
        fs->block_list = fat->empty_block_offsets;
        fs->list_len = fat->num_empty_blocks;
        fs->data_start_off = fat->data_start_off; //data start in clusters
	mks_debug("This is indeed FAT32");
        mks_debug("Data offset %d\n", fat->data_start_off);
	mks_debug("Number of data clusters, %u\n", fat->num_blocks);   
    	return DM_MKS_TRUE;
    }
    mks_debug("Not FAT32");
    return DM_MKS_FALSE;
}


