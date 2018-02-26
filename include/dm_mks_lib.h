/*
 * Common header for all library calls supported for 
 * Matryoshka. All filesystem support make their API
 * public through this header.
 * 
 * Author:
 * Copyright:
 */
#include <linux/string.h>

#ifndef _DM_MKS_LIB_H_
#define _DM_MKS_LIB_H_

//
// Enumerations
//
// Boolean
typedef enum mks_boolean {
    DM_MKS_TRUE = 0,
    DM_MKS_FALSE
} mks_boolean_t;

struct fs_data{
	u32 *empty_block_offsets; //byte offsets for each block
	u32 data_start_off;       //probably only used with FAT
        u16 bytes_sec;            //bytes per sector, usually 512
	u8 sec_block;             //sectors per block
	u32 bytes_block;          //number of bytes per block
	u32 num_blocks;           //number of blocks
};
//
// Filesystem support
//
mks_boolean_t mks_fat32_detect(const void *data, struct fs_data *fs);
//mks_boolean_t mks_ext4_detect(const void *data);
//mks_boolean_t mks_ntfs_detect(const void *data);

#endif
