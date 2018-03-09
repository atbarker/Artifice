/*
 * Library calls for support of the NTFS filesystem
 * for matryoshka.
 * 
 * Author:
 * Copyright:
 */
#include <dm_mks_lib.h>

/**
 * Detect presence of a NTFS filesystem. This is done by
 * sifting through the binary data and looking for NTFS
 * headers.
 * 
 * @param   data    The data to look into.
 * 
 * @return  mks_boolean
 *  DM_MKS_TRUE     data is formatted as NTFS.
 *  DM_MKS_FALSE    data is not formatted as NTFS.
 */
mks_boolean_t
mks_ntfs_detect(const void *data)
{
    return DM_MKS_FALSE;
}
