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
    u32     *block_list;        //list of empty blocks
    u32     list_len;           //length of that list
	u8	    sectors_per_block;  //sectors in a block
	u32	    total_blocks;       //total number of blocks in the FS
    u32     data_start_off;     //data start offset in the filesystem (bypass reserved blocks)
	u8      blocks_in_tuple;    //blocks in a tuple
    u8      *allocation;        //allocation bitmap
};

//
// Filesystem support
//
mks_boolean_t mks_fat32_detect(const void *data, struct mks_fs_context *fs, struct block_device *device);
mks_boolean_t mks_ext4_detect(const void *data);
mks_boolean_t mks_ntfs_detect(const void *data);

#endif
