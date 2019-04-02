#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/time.h>
#include <asm/fpu/api.h>
#include "rs.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AUSTEN BARKER");

static int __init km_template_init(void){
    uint8_t *data[2];
    uint8_t *zeroed_data[2];
    uint8_t *entropy[2];
    uint8_t *carrier[4];
    struct timespec timespec1, timespec2;
    struct config *conf = kmalloc(sizeof(struct config), GFP_KERNEL);
    struct dm_afs_erasures *erasure = kmalloc(sizeof(struct dm_afs_erasures), GFP_KERNEL);

    printk(KERN_INFO "Inserting kernel module\n");
    initialize(conf, 2, 2, 4);

    data[0] = kmalloc(4096, GFP_KERNEL);
    data[1] = kmalloc(4096, GFP_KERNEL);
    entropy[0] = kmalloc(4096, GFP_KERNEL);
    entropy[1] = kmalloc(4096, GFP_KERNEL);
    carrier[0] = kmalloc(4096, GFP_KERNEL);
    carrier[1] = kmalloc(4096, GFP_KERNEL);
    carrier[2] = kmalloc(4096, GFP_KERNEL);
    carrier[3] = kmalloc(4096, GFP_KERNEL);
    get_random_bytes(data[0], 4096);
    get_random_bytes(data[1], 4096);
    get_random_bytes(entropy[0], 4096);
    get_random_bytes(entropy[1], 4096); 

    memset(carrier[0], 0, 4096);
    memset(carrier[1], 0, 4096);
    memset(carrier[2], 0, 4096);
    memset(carrier[3], 0, 4096);

    getnstimeofday(&timespec1);
    dm_afs_encode(conf, data, entropy, carrier);
    getnstimeofday(&timespec2);
    printk(KERN_INFO "\n Encode took: %ld nanoseconds",
(timespec2.tv_sec - timespec1.tv_sec) * 1000000000 + (timespec2.tv_nsec - timespec1.tv_nsec));

    erasure->codeword_size = 8;
    erasure->num_erasures = 0;
    erasure->erasures[0] = 0;
    erasure->erasures[1] = 1;

    dm_afs_decode(conf, erasure, data, entropy, carrier); 
    /*//getnstimeofday(&time_spec1);
    memcpy(encoding, data, 62);
    memcpy(&encoding[62], entropy, 62);
    getnstimeofday(&time_spec1);
    kernel_fpu_begin();
    encode_rs(encoding, 124, &encoding[124] ,131);
    kernel_fpu_end();
    getnstimeofday(&time_spec2);
    memcpy(carrier, &encoding[124], 131);
    //getnstimeofday(&time_spec2);
    printk(KERN_INFO "\n MY_DBG : read took: %ld nanoseconds",
(time_spec2.tv_sec - time_spec1.tv_sec) * 1000000000 + (time_spec2.tv_nsec - time_spec1.tv_nsec));
    get_random_bytes(encoding, 124);
    memset(&encoding[124], 0, 131);
    rs_decoder = init_rs(8, 0x11D, 0, 1, 32);
    encode_rs8(rs_decoder, encoding, 223, carrier, 0);
    print_hex_dump(KERN_DEBUG, "encoding:", DUMP_PREFIX_OFFSET, 20, 1, (void*)encoding, 255, true);
    print_hex_dump(KERN_DEBUG, "carrier: ", DUMP_PREFIX_OFFSET, 20, 1, (void*)carrier, 131, true);*/

    kfree(conf);
    kfree(data[0]);
    kfree(data[1]);
    kfree(entropy[0]);
    kfree(entropy[1]);
    kfree(carrier[0]);
    kfree(carrier[1]);
    kfree(carrier[2]);
    kfree(carrier[3]);
    return 0;
}

static void __exit km_template_exit(void){
    //cleanup_rs();

    printk(KERN_INFO "Removing kernel module\n");
}

module_init(km_template_init);
module_exit(km_template_exit);
