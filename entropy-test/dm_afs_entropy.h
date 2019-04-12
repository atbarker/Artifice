#ifndef ENTROPY_H
#define ENTROPY_H

#include <linux/slab.h>

//Hash table of size 2^16 with name dm_afs_ht
//hopefully don't need anything bigger than that
#define HASH_TABLE_ORDER 16
#define HASH_TABLE_NAME dm_afs_ht

//entry in the hash table
struct entropy_hash_entry{
    uint64_t key;
    char* filename;
    size_t file_size;
    struct hlist_node hash_list;
};

struct entropy_context{
    uint32_t number_of_files;
    char* directory_name;
    size_t directory_name_length;
    char** file_list;
};

void build_entropy_ht(char* directory_name, size_t name_length);

void cleanup_entropy_ht(void);

void allocate_entropy(uint64_t filename_hash, uint32_t block_pointer);

int read_entropy(uint64_t filename_hash, uint32_t block_pointer, uint8_t* block);


#endif
