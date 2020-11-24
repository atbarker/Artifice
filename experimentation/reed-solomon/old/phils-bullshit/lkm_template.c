#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/time.h>
#include <asm/fpu/api.h>
//#include "rs.h"
#include <linux/rslib.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AUSTEN BARKER");

static int __init km_template_init(void){
    uint8_t data[62];
    uint8_t entropy[62];
    //uint8_t carrier[131];
    uint8_t *encoding = kmalloc(255, GFP_KERNEL);
    struct timespec time_spec1, time_spec2;
    static struct rs_control *rs_decoder;
    uint16_t *carrier = kmalloc(131, GFP_KERNEL);
    /*init_rs(124);
    //getnstimeofday(&time_spec1);
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
(time_spec2.tv_sec - time_spec1.tv_sec) * 1000000000 + (time_spec2.tv_nsec - time_spec1.tv_nsec));*/
    get_random_bytes(encoding, 124);
    memset(&encoding[124], 0, 131);
    rs_decoder = init_rs(8, 0x11D, 0, 1, 32);
    encode_rs8(rs_decoder, encoding, 223, carrier, 0);
    print_hex_dump(KERN_DEBUG, "encoding:", DUMP_PREFIX_OFFSET, 20, 1, (void*)encoding, 255, true);
    print_hex_dump(KERN_DEBUG, "carrier: ", DUMP_PREFIX_OFFSET, 20, 1, (void*)carrier, 131, true);
    printk(KERN_INFO "Inserting kernel module\n");
    return 0;
}

static void __exit km_template_exit(void){
    //cleanup_rs();

    printk(KERN_INFO "Removing kernel module\n");
}

module_init(km_template_init);
module_exit(km_template_exit);
