/**
 * Basic utility system for miscellaneous functions.
 * 
 * Author: Yash Gupta <ygupta@ucsc.edu> Austen Barker <atbarker@ucsc.edu>
 * Copyright: UC Santa Cruz, SSRC
 */
#include <dm_mks_utilities.h>
#include <dm_mks_lib.h>
#include <linux/errno.h>
#include <linux/crypto.h>
#include <crypto/hash.h>

#define BYTE_OFFSET(b) ((b) / 8)
#define BIT_OFFSET(b) ((b) % 8)

/**
 *Set of functions for interfacing with a basic bitmap.
 *Takes in our array of bits and the bit index one wishes to manipulate
 *
 *
 */
void set_bitmap(u8 *bits, int n){
    bits[BYTE_OFFSET(n)] |= (1 << BIT_OFFSET(n));
}

void clear_bitmap(u8 *bits, int n){
    bits[BYTE_OFFSET(n)] &= ~(1 << BIT_OFFSET(n));
}

int get_bitmap(u8 *bits, int n){
    int bit = bits[BYTE_OFFSET(n)] & (1 << BIT_OFFSET(n));
    return bit;
}

/**
 * Used as a callback function to signify the completion 
 * of an IO transfer to a block device.
 * 
 * @param   bio     The bio which has completed.
 */
static void 
mks_blkdev_callback(struct bio *bio)
{
    struct completion *event = bio->bi_private;

    mks_debug("completed event: %p\n", event);
    complete(event);
}

/**
 * A utility function to conveniently perform I/O from a 
 * specified block device. It masks away the asynchronous 
 * nature of the kernel to provide pseudo-synchronous 
 * kernel block IO.
 * 
 * @param   io_request  Matryoshka I/O request structure.
 * @param   flag        Flag to specify read or write.
 * 
 * @return  0       Successfully performed the I/O.
 * @return  <0      Error.
 */
int
mks_blkdev_io(struct mks_io *io_request, enum mks_io_flags flag)
{
    const int page_offset = 0;
    int ret;
    struct bio *bio = NULL;
    struct completion event;

    /*
     * Any IO to a block device in the kernel is done using bio.
     * We need to allocate a bio with a single IO vector and set
     * its corresponding fields.
     * 
     * bio's are asynchronous and hence an event based waiting
     * loop is used to provide the illusion of synchronous IO.
     */
    init_completion(&event);

    bio = bio_alloc(GFP_NOIO, 1);
    if (IS_ERR(bio)) {
        ret = PTR_ERR(bio);
        mks_alert("bio_alloc failure {%d}\n", ret);
        return ret;
    }

    switch (flag) {
        case MKS_IO_READ:
            bio->bi_opf |= REQ_OP_READ;
            break;
        case MKS_IO_WRITE:
            bio->bi_opf |= REQ_OP_WRITE;
            break;
        default:
            goto invalid_flag;
    }
    bio->bi_opf |= REQ_SYNC;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 2)
    bio->bi_disk = io_request->bdev->bd_disk;
#else
    bio->bi_bdev = io_request->bdev;
#endif

    bio->bi_iter.bi_sector = io_request->io_sector;
    bio->bi_private = &event;
    bio->bi_end_io = mks_blkdev_callback;
    bio_add_page(bio, io_request->io_page, io_request->io_size, page_offset);
    
    submit_bio(bio);
    wait_for_completion(&event);

    return 0;

invalid_flag:
    bio_endio(bio);
    return -EINVAL;
}

struct sdesc{
    struct shash_desc shash;
    char ctx[];
};

//generate a hash of the passphrase for locating or generating the superblock
static struct sdesc *init_sdesc(struct crypto_shash *alg){
    struct sdesc *sdesc;
    int size;
    size = sizeof(struct shash_desc) + crypto_shash_descsize(alg);
    sdesc = kmalloc(size, GFP_KERNEL);
    sdesc->shash.tfm = alg;
    sdesc->shash.flags = 0x0;
    return sdesc;
}

int passphrase_hash(unsigned char *passphrase, unsigned int pass_len, unsigned char *digest){
    struct crypto_shash *alg;
    char *hash_alg_name = "sha256";
    int ret;
    struct sdesc *sdesc;

    alg = crypto_alloc_shash(hash_alg_name, CRYPTO_ALG_TYPE_SHASH, 0);
    if(IS_ERR(alg)){
        mks_alert("Issue creating hash algorithm\n");
        return -1;
    }
    
    sdesc = init_sdesc(alg);
    if(IS_ERR(sdesc)){
        mks_alert("Could not generate hash description\n");
        return -1;
    }

    ret = crypto_shash_digest(&sdesc->shash, passphrase, pass_len, digest);
    if(ret != 0){
        mks_alert("Error Computing hash\n");
    }
    kfree(sdesc);
    crypto_free_shash(alg);
    return 0;
}

//should execute whilst we are generating a new superblock such that the superblock can include the location
int write_new_map(u32 entries, struct mks_fs_context *context){
    //figure out how many blocks are needed to write the map
    //determine locations for the matryoshka map blocks
    //mark those in the bitmap and record in each block data structure

    int i;
    //use the number of entries, block size, and entry sizes to calculate the number of blocks needed, these will then be formated
    //for each block, and for each matryoshka map entry, generate random numbers to determine the carrier block location on the disk
    for(i = 0; i < entries; i++){

    }

    //write the blocks to specific locations on disk
    //done
    //map doesn't need to be stored and can be regenerated during the repair process
    return 0;
}

//retrieve the matryoshka map from disk and save into memory in order to speed up some stuff.
int retrieve_map(struct mks_fs_context *context, struct block_device *device){
    //calculate how many blocks are needed to store the map
    int i = 0;
    int block_number = 0; 
    //execute a for loop over every logical block number
    for(i = 0; i<block_number; i++){

    }
    return 0;
}

//get a pointer to a new superblock
//TODO: add a field for a tuple of the other superblock block offsets
struct mks_super * generate_superblock(unsigned char *digest, u64 mks_size, u8 ecc_scheme, u8 secret_split_type, u32 mks_map_start){
    struct mks_super *super;
    super = kmalloc(sizeof(struct mks_super), GFP_KERNEL);
    memcpy(super->hash, (void*)digest ,32);
    super->mks_size = mks_size;
    super->ecc_scheme = ecc_scheme;
    super->secret_split_type = secret_split_type;
    super->mks_map_start = mks_map_start;
    return super;
}

//write the superblock to the disk in a set number of locations
int write_new_superblock(struct mks_super *super, int duplicates, unsigned char *digest, struct mks_fs_context *context, struct block_device *device){
    //u32 location[duplicates];
    int i;
    struct page *page;
    const u32 read_length = 1 << PAGE_SHIFT;
    void *data;
    int ret;
    struct mks_io io = {
        .bdev = device,
        .io_sector = (context->block_list[0]*context->sectors_per_block) + context->data_start_off,
        //.io_sector = 40,
        .io_size = read_length  
    };
    page = alloc_page(GFP_KERNEL);
    if (IS_ERR(page)) {
        ret = PTR_ERR(page);
        mks_alert("alloc_page failure {%d}\n", ret);
        return ret;
    }
    data = page_address(page);
    io.io_page = page;
    mks_debug("context block %p\n", context->block_list);
    //compute the hash 8 times and populate  the requisite array, use modulo to determine block offsets
    //this is nondeterministic, must find a more reliable way to do it over the search space
    //slightly different superblock for each copy on the disk
    mks_debug("superblock %p\n", super);
    memcpy(data, super, sizeof(struct mks_super));
    //write duplicate number of times to those locations on the disk
    for(i = 0; i < duplicates; i++){
        ret = mks_blkdev_io(&io, MKS_IO_WRITE);
        if(ret){
            mks_alert("Error when writing superblock copy {%d}\n", i);
            return ret;
        }
        mks_alert("Superblock written {%d}\n", i);
        //io.io_sector = ;
    }
    __free_page(page);
    return 0;
}

//retrieve the superblock from hashes of the passphrase
struct mks_super* retrieve_superblock(int duplicates, unsigned char *digest, struct mks_fs_context *context, struct block_device *device){
    int i;
    const u32 read_length = 1 << PAGE_SHIFT;
    struct page *page;
    struct mks_super *super = kmalloc(sizeof(struct mks_super), GFP_KERNEL);
    void *data;
    int ret;
    struct mks_io io = {
        .bdev = device,
        .io_sector = (context->block_list[0]*context->sectors_per_block)+ context->data_start_off,
        .io_size = read_length  
    };

    page = alloc_page(GFP_KERNEL);
    if (IS_ERR(page)) {
        ret = PTR_ERR(page);
        mks_alert("alloc_page failure {%d}\n", ret);
        return NULL;
    }
    data = page_address(page);
    io.io_page = page;
    //compute the first hash and check the location on the disk, if the prefix matches the specific hash then we are good.

    //check locations
    for(i = 0; i < duplicates; i++){
	//put something here to repair the superblock when it is written to the disk
	//if we know that the block is here we can break out of the loop and return.
        ret = mks_blkdev_io(&io, MKS_IO_READ);
        if (ret) {
            mks_alert("mks_blkdev_io failure, Could not read superblock {%d}\n", ret);
            return NULL;
        }
        memcpy(super, data, sizeof(struct mks_super));
        mks_debug("Superblock %p\n", super);
        mks_debug("Hash %d\n", super->hash[0]);
        if(super->hash[0] == digest[0]){
            mks_debug("Superblock copy {%d} found\n", i);
            break;
	}
        mks_debug("Superblock copy {%d} not found\n", i);
    }
    if(i == duplicates){
        mks_alert("superblock not found, either overwritten or no instance ever existed.\n");
        return NULL;
    }
    __free_page(page);
    return super;
}

//rewrite new copies of the superblock to bring us up to spec
int superblock_repair(struct mks_fs_context *context, struct block_device *device){
    return 0;
}

//run repair cycle on the matryoshka map
int map_repair(struct mks_fs_context *context, struct block_device *device){
    return 0;
}

//takes in the matryoshak map and the free list of blocks, then returns a physical block tuple based on logical block number
int find_physical_block(struct mks_fs_context *context, struct block_device *device){
    return 0;
}


/**
 * Perform a reverse bit scan for an unsigned long.
 */
inline unsigned long
bsr(unsigned long n)
{
	__asm__("bsr %1,%0" : "=r" (n) : "rm" (n));
	return n;
}
