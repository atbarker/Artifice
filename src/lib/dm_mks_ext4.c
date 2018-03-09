/*
 * Library calls for support of the EXT4 filesystem
 * for matryoshka.
 * 
 * Author:
 * Copyright:
 */
#include <dm_mks_lib.h>

/**
 * Detect presence of a EXT4 filesystem. This is done by
 * sifting through the binary data and looking for FAT32
 * headers.
 * 
 * @param   data    The data to look into.
 * 
 * @return  mks_boolean
 *  DM_MKS_TRUE     data is formatted as EXT4.
 *  DM_MKS_FALSE    data is not formatted as EXT4.
 */
mks_boolean_t
mks_ext4_detect(const void *data)
{
    return DM_MKS_FALSE;
}
