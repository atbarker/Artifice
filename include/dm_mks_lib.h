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

//
// Filesystem support
//
mks_boolean_t mks_fat32_detect(const void *data);

#endif