/*
 * Library calls for support of the FAT32 filesystem
 * for matryoshka.
 *
 * It only supports FAT32, no FAT16 or FAT12.
 * 
 * Author:
 * Copyright:
 */
#include <dm_mks_lib.h>
#include <dm_mks_utilities.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/slab.h>

//This is hell incarnate, all the information about a FAT volume
struct fat_volume{

	void *fat_map;              //FAT mapped into memory
	u32 *empty_clusters;        //list of empty clusters (blocks)
	u32 num_data_clusters;      //Number of data cluster on the disk
	off_t data_start_off;       //byte offset for the second cluster
	size_t num_alloc_files;     //number of allocated files
	size_t max_allocated_files; //number of allocatable files
	char oem_name[8+1];         //boot sector information

	//data from the DOS 2.0 parameter block
	u16 bytes_sector;           //bytes in a sector
	u16 sector_order;           //sector order
	u8 sec_cluster;             //sectors in cluster
	u8 sec_cluster_order;       //sector cluster order
	u16 cluster_order;          //cluster order
	u16 reserved;               //reserved sectors
	u8 tables;                  //number of fat tables
	u16 root_entries;           //max root entries
	u8 media_desc;              //media description
	
	u32 total_sec;              //total number of sectors
	u32 sec_fat;                //sectors per FAT

	//Data from DOS 3.0 block
	u16 sec_track;              //sectors per track
	u16 num_heads;              //number of heads
	u32 hidden_sec;             //number of hidden sectors

	//FAT32 extended;
	u16 driv_desc;              //drive description
	u16 version;                //drive version
	u32 root_dir_start;         //start of the root directory
	u16 fs_info_sec;            //info sector offset
	u16 alt_boot_sec;           //alternate boot sector

	//nonFAT32 extended
	u8 phys_driv_num;           //physical drive number
	u8 ext_boot_sig;            //boot signature
	u32 vol_id;                 //volume id
	char volume_label[11+1];    //volume label
	char fs_type[8 + 1];        //FS type

};

/*Extended BIOS parameter block for FAT32 */
struct fat32_ebpb{

	__le32 sec_fat;               //sectors per fat
	__le16 drive_desc;            //drive description
	__le16 version;               //version
	__le32 root_start_clust;      //starting cluster for the root dir.
	__le16 fs_info_sec;           //info sector
	__le16 alt_boot_sec;          //alternate boot sector
	u8 reserved[12];

} __attribute__((packed));

struct nonfat32_ebpb{
	u8 physical_drive_num;
	u8 reserved;
	u8 extended_boot_sig;
	__le32 volume_id;
	char volume_label[11];
	char fs_type[8];
} __attribute__((packed));

/*FAT32 boot sector, exactly how it appears on your disk*/
struct fat_boot_sector{
        
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


//TODO: Kill this, find something better than this
inline unsigned long bsr(unsigned long n){
	__asm__("bsr %1,%0" : "=r" (n) : "rm" (n));
	return n;
}


static int fat_read_dos_2_0_bpb(struct fat_volume *vol, const struct fat_boot_sector *boot_sec){
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

static int 
fat_read_dos_3_31_bpb(struct fat_volume *vol, const struct fat_boot_sector *boot_sec){
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

static int 
fat_read_nonfat32_ebpb(struct fat_volume *vol, const struct nonfat32_ebpb *ebpb){
	vol->phys_driv_num = ebpb->physical_drive_num;
	vol->ext_boot_sig = ebpb->extended_boot_sig;
	vol->vol_id = le32_to_cpu(ebpb->volume_id);
	memcpy(vol->volume_label, ebpb->volume_label, sizeof(ebpb->volume_label));
	memcpy(vol->fs_type, ebpb->fs_type, sizeof(ebpb->fs_type));
	//debug here
	return 0;
}

static int 
fat_read_fat32_ebpb(struct fat_volume *vol, const struct fat32_ebpb *ebpb){
	
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

static int 
read_boot_sector(struct fat_volume *vol, const void *data){

	//u8 buf[512];
	struct fat_boot_sector *boot_sec = kmalloc(512, GFP_KERNEL);
	int ret;
	u32 num_data_sectors;

	//copy first 512 bytes into the fat_boot_sector
	memcpy(boot_sec, data, 512);

	memcpy(vol->oem_name, boot_sec->oem_name, sizeof(boot_sec->oem_name));

	//TODO remove trailing spaces from vol->oem_name
	
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

static int 
fat_map(struct fat_volume *vol, void *data, struct block_device *device){
	
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
	struct page *page;
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

	start_sector = (sector_t)(fat_aligned_offset/512);
	
	if(fat_aligned_offset > 4096){
		mks_debug("Read more data to map the FAT\n");
		page = alloc_page(GFP_KERNEL);
                fat_data = page_address(page);
		status = mks_read_blkdev(device, page, start_sector, page_size);
		mks_debug("FAT read successfully.\n");	
	}
	
	//check the aligned size for errors
	
	vol->fat_map = data + fat_aligned_offset;

	//
	p = (int*)vol->fat_map;
	cluster_number = 0;
	empty_clusters = kmalloc(fat_size_bytes, GFP_KERNEL);
	//uint8_t *cluster_contents = kmalloc(1, GFP_KERNEL);
	for (i=0; i<vol->num_data_clusters; i++){
		if(p[i] == 0){
			empty_clusters[cluster_number] = i;
		}
	}
	vol->empty_clusters = empty_clusters;
	return 0;
}

struct fs_data * 
mks_fat32_parse(void *data, struct block_device *device){

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

	parameters->num_blocks = vol->num_data_clusters;
	parameters->bytes_sec = vol->bytes_sector;
	parameters->sec_block = vol->sec_cluster;
	parameters->bytes_block = vol->bytes_sector * vol->sec_cluster;
	parameters->data_start_off = (u32)vol->data_start_off;
	parameters->empty_block_offsets = vol->empty_clusters;

	return parameters;
}

struct page * 
fat_next_cluster(const struct fat_volume *vol, u32 cluster){
	return 0;
}

int 
fat_is_valid_cluster_offset(const struct fat_volume *vol, u32 cluster){
	return 0;
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

mks_boolean_t mks_fat32_detect(const void *data, struct fs_data *fs, struct block_device *device)
{
    fs = mks_fat32_parse((void *)data, device);
    if(fs){
	mks_debug("This is indeed FAT32");
	//mks_debug("Number of data clusters, %u\n", fs->num_blocks);   
    	return DM_MKS_TRUE;
    }
    mks_debug("Not FAT32");
    return DM_MKS_FALSE;
}


