/*
 * Common header for all library calls supported for 
 * Matryoshka. All filesystem support make their API
 * public through this header.
 * 
 * Author: Austen Barker, Yash Gupta
 * Copyright: UC Santa Cruz SSRC
 */
#include <linux/string.h>
#include <linux/bio.h>

#ifndef _DM_MKS_LIB_H
#define _DM_MKS_LIB_H

//
// Enumerations
//
// Boolean
typedef enum mks_boolean {
    DM_MKS_TRUE = 0,
    DM_MKS_FALSE
} mks_boolean_t;

//
// Structures
//
// File system information required.
struct mks_fs_context {
	struct {
		/*
		 * Since the width of our array is 32 bits, we cannot
		 * hold a block number larger than ~4 billion. We also
		 * do not expect the total number of free blocks to ever
		 * exceed ~4 billion.
		 * 
		 * NOTE: In case we want to support disks > 16TB, we will
		 * need to add an array which is 64 bits wide. This can be
		 * done with a union.
		 */

		u32	*block_list;
		u32	list_len;
	} free_blocks;

	u16	sectors_per_block;
	u64	total_blocks;
};

// TODO: Obselete (Move FAT32 specific data into fat32 library)
struct fs_data {
	u32	*empty_block_offsets; 	//byte offsets for each block
	u32 data_start_off;       	//probably only used with FAT
    u16 bytes_sec;            	//bytes per sector, usually 512
	u8	sec_block;            	//sectors per block
	u32 bytes_block;          	//number of bytes per block
	u32 num_blocks;           	//number of blocks
};

//
// Filesystem support
//
mks_boolean_t mks_fat32_detect(const void *data, struct fs_data *fs, struct block_device *device);
mks_boolean_t mks_ext4_detect(const void *data);
mks_boolean_t mks_ntfs_detect(const void *data);

#endif
