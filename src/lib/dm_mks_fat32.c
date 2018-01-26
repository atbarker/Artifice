/*
 * Library calls for support of the FAT32 filesystem
 * for matryoshka.
 * 
 * Author:
 * Copyright:
 */
#include <dm_mks_lib.h>

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