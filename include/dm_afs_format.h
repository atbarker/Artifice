/**
 * Author: Yash Gupta <ygupta@ucsc.edu>, Austen Barker <atbarker@ucsc.edu>
 * Copyright: UC Santa Cruz, SSRC
 */
#include <dm_afs_config.h>

#ifndef DM_AFS_FORMAT_H
#define DM_AFS_FORMAT_H

// Artifice super block.
struct __attribute__((packed)) afs_super_block {
    uint8_t sb_hash[SHA256_SZ];                  // Hash of the superblock.
    uint8_t hash[SHA1_SZ];                       // Hash of the passphrase.
    uint64_t instance_size;                      // Size of this Artifice instance.
    uint8_t reserved[4];                         // TODO: Replace with RS information.
    char entropy_dir[ENTROPY_DIR_SZ];            // Entropy directory for this instance.
    char shadow_passphrase[PASSPHRASE_SZ];       // In case this instance is a nested instance.
    uint32_t map_block_ptrs[NUM_MAP_BLKS_IN_SB]; // The super block stores the pointers to the first 975 map blocks.
    uint32_t first_ptr_block;                    // Pointer to the first pointer block in the chain.
};

// Artifice pointer block.
struct __attribute__((packed)) afs_ptr_block {
    // Each pointer block is 4KB. With 32 bit pointers,
    // we can store 1019 pointers to map blocks and
    // a pointer to the next block.

    uint8_t hash[SHA128_SZ];
    uint32_t map_block_ptrs[NUM_MAP_BLKS_IN_PB];
    uint32_t next_ptr_block;
};

// Artifice map tuple.
struct __attribute__((packed)) afs_map_tuple {
    uint32_t carrier_block_ptr; // Sector number from the free list.
    uint32_t entropy_block_ptr; // Sector number from the entropy file.
    uint16_t checksum;          // Checksum of this carrier block.
};

// struct afs_map_entry
//
// We cannot create a struct out of this since
// the number of carrier blocks is user defined.
//
// Overall Size: 24 + (10 * num_carrier_blocks) bytes.
//
// Structure:
// afs_map_tuple[0] (10 bytes)
// afs_map_tuple[1] (10 bytes)
// .
// .
// .
// afs_map_tuple[num_carrier_blocks-1] (10 bytes)
// hash (16 bytes)
// Entropy file name hash (8 bytes)

// struct afs_map_block
//
// We cannot create a struct out of this since
// the size of an afs_map_entry is variable based
// on user configuration.
//
// Overall Size: Exactly 4096 bytes.
//
// Structure:
// hash (64 bytes)
// unused space (unused_space_per_block bytes)
// afs_map_entry[0] (map_entry_sz bytes)
// afs_map_entry[1] (map_entry_sz bytes)
// .
// .
// .
// afs_map_entry[num_map_entries_per_block-1] (map_entry_sz bytes)

#endif /* DM_AFS_FORMAT_H */