#ifndef ENTROPY_H
#define ENTROPY_H

#include <linux/slab.h>

void build_entropy_ht(char* directory_name);

void allocate_entropy(uint8_t* filename_hash, uint32_t block_pointer);

int read_entropy(uint8_t* filename_hash, uint32_t block_pointer, uint8_t* block);

#endif
