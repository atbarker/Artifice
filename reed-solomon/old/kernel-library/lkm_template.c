#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/rslib.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include "reedsolomon.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AUSTEN BARKER");

static int __init km_template_init(void){
    //uint16_t par[6];
    struct afs_rs_config *config;
    struct afs_rs_block *data;
    struct afs_rs_block *carrier;
    struct afs_rs_block *entropy;

    config = vmalloc(sizeof(struct afs_rs_config));
    data = vmalloc(sizeof(struct afs_rs_block));
    carrier = vmalloc(sizeof(struct afs_rs_block));
    entropy = vmalloc(sizeof(struct afs_rs_block));
    config->block_size = 512;
    config->num_carrier = 1;
    config->num_entropy = 1;
    config->num_data = 1;
    config->num_reconstruct = 1;
    data->blocks = vmalloc(config->block_size);
    data->block_set = 1;
    data->block_size = config->block_size;
    carrier->blocks = vmalloc(config->block_size);
    carrier->block_set = 1;
    carrier->block_size = 1;
    entropy->blocks = vmalloc(config->block_size);

    memset(data->blocks, 0, config->block_size);
    memset(carrier->blocks, 0, 1024);
    printk(KERN_INFO "THIS IS A KERNEL MODULE\n");
    //SHOULD BE HALF OF DESIRED BYTES
    initialize_rs(512);
    get_random_bytes(data->blocks, config->block_size);
    get_random_bytes(entropy->blocks, config->block_size);

    encode(config, data, entropy, carrier);
    print_hex_dump(KERN_DEBUG, "carrier: ", DUMP_PREFIX_OFFSET, 20, 1, (void*)carrier->blocks, 1024, true); 
    get_random_bytes(data->blocks, 512); 
    //decode(config, data, entropy, carrier);
    vfree(data->blocks);
    vfree(carrier->blocks);
    vfree(entropy->blocks);
    vfree(config);
    vfree(entropy);
    vfree(data);
    vfree(carrier);
    return 0;
}

static void __exit km_template_exit(void){
    //cleanup_rs();
    printk(KERN_INFO "Removing kernel module\n");
}

module_init(km_template_init);
module_exit(km_template_exit);
