#ifndef REED_H
#define REED_H

#include <linux/rslib.h>

static struct rs_control *rs_decoder;

//struct for an array of blocks
struct afs_rs_block{
    uint32_t block_size;  //block size in bytes
    uint8_t  block_set;   //number of blocks in this set
    uint8_t  *blocks;    //2d array of bytes containing all blocks
};

struct afs_rs_config{
    uint32_t block_size;
    uint8_t  num_carrier;
    uint8_t  num_entropy;
    uint8_t  num_data;
    uint8_t  num_reconstruct;
};

//number of symbols is how many to recover
void initialize_rs(int num_symbols);

//int list_to_buffer(uint8_t **blocks, uint32_t length, struct afs_rs_block *blocks);

int encode(struct afs_rs_config *config, struct afs_rs_block *data, struct afs_rs_block *entropy, struct afs_rs_block *carrier);

//int encode(uint32_t data_length, uint8_t *data, void *entropy, uint32_t par_length, uint16_t *par);

int decode(struct afs_rs_config *config, struct afs_rs_block *data, struct afs_rs_block *entropy, struct afs_rs_block *carrier); 

//int decode(uint32_t data_length, uint8_t *data, void *entropy, uint32_t par_length, uint16_t *par);

void cleanup_rs(void);

#endif
