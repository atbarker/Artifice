/*
 *This is the file containing functions for handling fat32 metadata processing.
 *These functions read in the metadata and returns structures containing free
 *space block offsets and the contents of the passive file system's superblock.
 * 
 */
//linux kernel headers
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>

//dm-mks headers
#include "fat32_read.h"
