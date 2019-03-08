
#include <linux/rslib.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include "reedsolomon.h"

void initialize_rs(int num_symbols){
    rs_decoder = init_rs(12, 0x1FBB, 0, 1, num_symbols);
}

//TODO: This should take in a set of entropy blocks, data blocks, and return parity
int encode(struct afs_rs_config *config, struct afs_rs_block *data, struct afs_rs_block *entropy, struct afs_rs_block *carrier){
    uint8_t *input_buf;
    uint32_t data_buf_size = (config->num_data * config->block_size);
    uint32_t entropy_buf_size = (config->num_entropy * config->block_size); 
    uint32_t encode_buf_size = (config->num_entropy + config->num_data) * config->block_size;
    //printk("%d\n", entropy_buf_size);
    //printk("%d\n", data_buf_size);
    //printk("%d\n", encode_buf_size); 
    input_buf = vmalloc(encode_buf_size);
    memcpy(input_buf, data->blocks, data_buf_size);
    memcpy(&input_buf[data_buf_size], entropy->blocks, entropy_buf_size);    
    encode_rs8(rs_decoder, input_buf, (encode_buf_size / 2), (uint16_t*)carrier->blocks, 0);
    print_hex_dump(KERN_DEBUG, "data: ", DUMP_PREFIX_OFFSET, 20, 1, (void*)input_buf, data_buf_size, true);
    //print_hex_dump(KERN_DEBUG, "carrier: ", DUMP_PREFIX_OFFSET, 20, 1, (void*)carrier->blocks, 512, true);     
    vfree(input_buf);
    return 0;
}

//TODO: should take in entropy and parity blocks, spit out data
int decode(struct afs_rs_config *config, struct afs_rs_block *data, struct afs_rs_block *entropy, struct afs_rs_block *carrier){
    int numerr;
    int i = 0;
    int *err_loc;
    uint8_t *input_buf;
    uint32_t data_buf_size = (config->num_data * config->block_size);
    uint32_t entropy_buf_size = (config->num_data * config->block_size);
    uint32_t carrier_buf_size = (config->num_carrier * config->block_size);
    uint32_t decode_buf_size = (config->num_carrier + config->num_entropy) * config->block_size;
    err_loc = vmalloc(sizeof(int) * decode_buf_size);
    for(i = 0; i < config->block_size; i++){
        err_loc[i] = i;
    }
    input_buf = vmalloc(1024);
    //memcpy(input_buf, , config->block_size);
    memcpy(&input_buf[config->block_size], entropy->blocks, config->block_size);
    printk("%d\n", rs_decoder->nn);
    printk("%d\n", rs_decoder->mm);
    printk("%d\n", rs_decoder->nroots);
    numerr = rs_decoder->nn - rs_decoder-> nroots - 1024;
    printk("%d\n", numerr);
    numerr = decode_rs8(rs_decoder, input_buf, (uint16_t*)carrier->blocks, 1024, NULL, 512, err_loc, 0, NULL);
    print_hex_dump(KERN_DEBUG, "data: ", DUMP_PREFIX_OFFSET, 20, 1, (void*)input_buf, 1024, true);
    printk("%d\n", numerr);
    return numerr;
}


void cleanup_rs(void){
    free_rs(rs_decoder);
}
