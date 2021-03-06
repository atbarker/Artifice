#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/time.h>
#include <asm/fpu/api.h>
#include "dm_afs_entropy.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AUSTEN BARKER");

static int __init km_template_init(void){
    uint64_t filename_hash;
    uint32_t block_pointer;
    uint8_t* entropy_block = kmalloc(4096, GFP_KERNEL);

    printk(KERN_INFO "Inserting kernel module\n");

    build_entropy_ht("/usr/bin", 8);

    allocate_entropy(&filename_hash, &block_pointer, entropy_block);

    printk(KERN_INFO "Filename hash: %llu\n Block Pointer: %u\n", filename_hash, block_pointer);
    print_hex_dump(KERN_DEBUG, "entropy:", DUMP_PREFIX_OFFSET, 20, 1, (void*)entropy_block, 4096, true);
    cleanup_entropy_ht();
    kfree(entropy_block);
    return 0;
}

static void __exit km_template_exit(void){
    printk(KERN_INFO "Removing kernel module\n");
}

module_init(km_template_init);
module_exit(km_template_exit);
