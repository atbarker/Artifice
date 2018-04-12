/**
 * Basic utility system for miscellaneous functions.
 * 
 * Author: Yash Gupta <ygupta@ucsc.edu> Austen Barker <atbarker@ucsc.edu>
 * Copyright: UC Santa Cruz, SSRC
 */
#include <dm_mks_utilities.h>
#include <linux/errno.h>
#include <linux/crypto.h>
#include <crypto/hash.h>

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

int write_new_map(u32 entries, struct mks_private *context){
    return 0;
}

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
int write_new_superblock(struct mks_super *super, int duplicates, unsigned char *digest, struct mks_private *context){
    return 0;
}

//retrieve the superblock from hashes of the passphrase
struct mks_super* retrieve_superblock(int duplicates, unsigned char *digest, struct mks_private *context){
    return NULL;
}

//takes in the matryoshak map and the free list of blocks, then returns a physical block tuple based on logical block number
/*int find_physical_block(){
    return 0;
}*/


/**
 * Perform a reverse bit scan for an unsigned long.
 */
inline unsigned long
bsr(unsigned long n)
{
	__asm__("bsr %1,%0" : "=r" (n) : "rm" (n));
	return n;
}
