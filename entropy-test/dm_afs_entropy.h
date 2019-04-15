#ifndef ENTROPY_H
#define ENTROPY_H

#include <linux/slab.h>

//Hash table of size 2^16 with name dm_afs_ht
//hopefully don't need anything bigger than that
#define HASH_TABLE_ORDER 16
#define HASH_TABLE_NAME dm_afs_ht

/**
 * A single entry in the hash table
 * @key, the hashed filename
 * @filename, name of the specified file
 * @file_size, size of the specified file (used for allocation)
 * @hash_list, list of hash nodes needed for the hash table to work
 */
struct entropy_hash_entry{
    uint64_t key;
    char* filename;
    size_t file_size;
    struct hlist_node hash_list;
};

/**
 * Global entropy context for AFS
 * @number_of_files, number of files in the directory (non-recursive search, disregards directories)
 * @directory_name, user specified name of the entropy directory
 * @directory_name_length, used for bounds checking on strings
 * @file_list, list of file names in the directory
 */
struct entropy_context{
    uint32_t number_of_files;
    char* directory_name;
    size_t directory_name_length;
    char** file_list;
};

/**
 * Hash table constructor
 * Call in dm target constructor with entropy directory name as input
 */
void build_entropy_ht(char* directory_name, size_t name_length);

/**
 * Call in dm target destructor
 */
void cleanup_entropy_ht(void);

/**
 * Allocate an entropy block (if new blocks are needed)
 * Populates the variables for the filename hash, block pointer, and data block
 */
void allocate_entropy(uint64_t *filename_hash, uint32_t *block_pointer, uint8_t *entropy_block);

/**
 * Reads a specified entropy block from the disk
 */
int read_entropy(uint64_t filename_hash, uint32_t block_pointer, uint8_t* block);


#endif
