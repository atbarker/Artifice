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
    u32     *block_list;
    u32     list_len;
	u8	    sectors_per_block;
	u32	    total_blocks;
    u32     data_start_off;
	u8      blocks_in_tuple;
    u8      *allocation;
};

// TODO: Obselete (Move FAT32 specific data into fat32 library)
struct fs_data {
	u32	*empty_block_offsets; 	//byte offsets for each block
	u32 data_start_off;       	//probably only used with FAT
    u16 bytes_sec;            	//bytes per sector, usually 512
	u8	sec_block;            	//sectors per block
	u32 bytes_block;          	//number of bytes per block
	u32 num_blocks;           	//number of blocks
        u32 num_empty_blocks;
};

//
// Filesystem support
//
mks_boolean_t mks_fat32_detect(const void *data, struct mks_fs_context *fs, struct block_device *device);
mks_boolean_t mks_ext4_detect(const void *data);
mks_boolean_t mks_ntfs_detect(const void *data);

#endif
