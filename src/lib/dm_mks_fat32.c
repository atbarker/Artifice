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
#include <linux/kernel.h>
#include <linux/kmalloc.h>
#include <linux/errno.h>


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

} __attribute__((packed));

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
	__le16 mat_root_ent;          //maximum root entries
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
mks_fat32_detect(const void *data)
{
    
    return DM_MKS_TRUE;
}

extern struct fs_data * mks_fat32_parse(){

}

extern int fat_unmount(struct fat_volume *vol){

}

extern u32 fat_next_cluster(const struct fat_volume *vol, u32 cluster){

}

int fat_is_valid_cluster_offset(const struct fat_volume *vol, u32 cluster){

}
