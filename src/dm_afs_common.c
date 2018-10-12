/**
 * Author: Yash Gupta <ygupta@ucsc.edu>, Austen Barker <atbarker@ucsc.edu>
 * Copyright: UC Santa Cruz, SSRC
 */
#include <dm_afs_common.h>
#include <dm_afs_modules.h>
#include <linux/errno.h>
#include <linux/crypto.h>
#include <linux/random.h>
#include <linux/rslib.h>
#include <crypto/hash.h>

// #define BYTE_OFFSET(b) ((b) / 8)
// #define BIT_OFFSET(b) ((b) % 8)
 
// //speck definitions
// #define ROR(x, r) ((x >> r) | (x << (64 - r)))
// #define ROL(x, r) ((x << r) | (x >> (64 - r)))
// #define R(x, y, k) (x = ROR(x, 8), x += y, x ^= k, y = ROL(y, 3), y ^= x)
// #define ROUNDS 32

/**
 * Callback function to signify the completion 
 * of an IO transfer to a block device.
 */
static void 
__callback_blkdev_io(struct bio *bio)
{
    struct completion *event = bio->bi_private;

    afs_debug("completed event: %p\n", event);
    complete(event);
}

/**
 * Read or write to an block device.
 * 
 * @request  I/O request structure
 * @return  0       Successfully performed the I/O.
 * @return  <0      Error.
 */
int
afs_blkdev_io(struct afs_io *request)
{
    const int page_offset = 0;
    int ret;
    struct bio *bio = NULL;
    struct completion event;

    
    // Any IO to a block device in the kernel is done using bio.
    // We need to allocate a bio with a single IO vector and set
    // its corresponding fields.
    // 
    // bio's are asynchronous and hence an event based waiting
    // loop is used to provide the illusion of synchronous IO.
    init_completion(&event);

    bio = bio_alloc(GFP_NOIO, 1);
    afs_assert_action(!IS_ERR(bio), ret = PTR_ERR(bio), alloc_err, "could not allocate bio [%d]", ret);

    switch (request->type) {
        case IO_READ:
            bio->bi_opf |= REQ_OP_READ;
            break;

        case IO_WRITE:
            bio->bi_opf |= REQ_OP_WRITE;
            break;

        default:
            afs_assert_action(0, ret = -EINVAL, invalid_type, "invalid IO type [%d]", request->type);
    }
    bio->bi_opf |= REQ_SYNC;
    // TODO: Perhaps make ASYNC for performance considerations?

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 2)
    bio->bi_disk = request->bdev->bd_disk;
#else
    bio->bi_bdev = request->bdev;
#endif

    bio->bi_iter.bi_sector = request->io_sector;
    bio->bi_private = &event;
    bio->bi_end_io = __callback_blkdev_io;
    bio_add_page(bio, request->io_page, request->io_size, page_offset);
    
    submit_bio(bio);
    wait_for_completion(&event);

    return 0;

invalid_type:
    bio_endio(bio);

alloc_err:
    return ret;
}

/**
 * Acquire a SHA1 hash of given data.
 * 
 * @digest Array to return digest into. Needs to be pre-allocated 20 bytes.
 */
int 
hash_sha1(const uint8_t *data, const uint32_t data_len, uint8_t *digest)
{
    const char *alg_name = "sha1";
    struct shash_desc desc;
    int ret;
    
    desc.tfm = crypto_alloc_shash(alg_name, 0, CRYPTO_ALG_ASYNC);
    afs_assert_action(!IS_ERR(desc.tfm), ret = PTR_ERR(desc.tfm), done, "could not allocate algorithm");

    ret = crypto_shash_digest(&desc, data, data_len, digest);
    afs_assert(!ret, done, "error computing sha1 [%d]", ret);
    crypto_free_shash(desc.tfm);

done:
    return ret;
}

// /**
//  *Set of functions for interfacing with a basic bitmap.
//  *Takes in our array of bits and the bit index one wishes to manipulate
//  *
//  *
//  */
// //TODO: add bitmap length to these functions
// void set_bitmap(u8 *bits, int n){
//     bits[BYTE_OFFSET(n)] |= (1 << BIT_OFFSET(n));
// }

// void clear_bitmap(u8 *bits, int n){
//     bits[BYTE_OFFSET(n)] &= ~(1 << BIT_OFFSET(n));
// }

// int get_bitmap(u8 *bits, int n){
//     int bit = (bits[BYTE_OFFSET(n)] & (1 << BIT_OFFSET(n))) >> BIT_OFFSET(n);
//     return bit;
// }

// /*
//  *Returns a random offset
//  *Used for spreading out blocks in the matryoshka disk
//  */
// int random_offset(u32 upper_limit){
//     u32 i;
//     get_random_bytes(&i, sizeof(i));
//     return i % upper_limit;
// }

// void encryptSpeck(uint64_t ct[2], uint64_t const pt[2], uint64_t const K[2])
// {
//    uint64_t y = pt[0], x = pt[1], b = K[0], a = K[1];
//    int i = 0;
//    R(x, y, b);
//    for (i = 0; i < ROUNDS - 1; i++) {
//       R(a, b, i);
//       R(x, y, b);
//    }

//    ct[0] = y;
//    ct[1] = x;
// }

// struct sdesc{
//     struct shash_desc shash;
//     char ctx[];
// };

// //generate a hash of the passphrase for locating or generating the superblock
// static struct sdesc *init_sdesc(struct crypto_shash *alg){
//     struct sdesc *sdesc;
//     int size;
//     size = sizeof(struct shash_desc) + crypto_shash_descsize(alg);
//     sdesc = kmalloc(size, GFP_KERNEL);
//     sdesc->shash.tfm = alg;
//     sdesc->shash.flags = 0x0;
//     return sdesc;
// }

// //should execute whilst we are generating a new superblock such that the superblock can include the location
// //returns offset of the first block written to the disk
// //TODO: CHange the context passed in here.
// struct afs_map_entry* write_new_map(u32 entries, struct afs_fs_context *context, struct block_device *device, u32 first_offset){
//     //figure out how many blocks are needed to write the map
//     //determine locations for the matryoshka map blocks
//     //mark those in the bitmap and record in each block data structure

//     int i, j, k;
//     int tuple_size = 8;
//     u32 entry_size = (32 + tuple_size*(32) + 128)/8;
//     u32 block_size = context->sectors_per_block * 512;
//     u32 entries_per_block = block_size / entry_size;
//     u32 blocks = (entries / entries_per_block) + 1;
//     u32 map_offsets[blocks];
//     int entry_size_32 = sizeof(struct afs_map_entry) / 4;
//     int block_offset;
//     int ret;
//     void *data;
//     const u32 read_length = 1 << PAGE_SHIFT;
//     struct page *page;
//     struct afs_io io = {
//         .bdev = device,
//         .io_size = read_length
//     };
//     struct afs_map_entry *map_block;

//     //ensure that we have 48 bits left over for map pointer and checksums
//     //32 bits for the pointer and 16 for the checksum (could be changed)
//     if(block_size - (entry_size * entries_per_block) < entry_size){
//         entries_per_block -= 1;
//         blocks = (entries / entries_per_block) + 1;
//     }
    
//     afs_debug("Space ensured for pointer\n");

//     page = alloc_page(GFP_KERNEL);
//     data = page_address(page);
//     io.io_page = page;

//     block_offset = random_offset(100);
//     map_offsets[0] = first_offset;
//     for(i = 1; i < blocks; i++){
//         while(get_bitmap(context->allocation, block_offset) != 0){
//             block_offset = block_offset + random_offset(100);
//             if(block_offset > context->list_len){
//                 block_offset = random_offset(100);
//             }
//         }
//         map_offsets[i] = block_offset;
//         set_bitmap(context->allocation, block_offset);
//         block_offset = block_offset + random_offset(100);
//     } 
//     afs_debug("offsets generated\n");
 
//     //use the number of entries, block size, and entry sizes to calculate the number of blocks needed
//     //for each block, and for each matryoshka map entry, generate random numbers to determine the carrier block location on the disk
//     //fills out the bitmap
//     //TODO: bug in this block
//     map_block = kmalloc(entries * sizeof(struct afs_map_entry), GFP_KERNEL);
//     afs_debug("Allocated space for the map\n");
//     afs_debug("Number of entries in map: %d\n", entries);
//     block_offset = random_offset(100);
//     afs_debug("Initial block offset: %d\n", block_offset);
//     for(j = 0; j < entries; j++){
//         //find locations for each block
//         for(k = 0; k < tuple_size; k++){
//             while(get_bitmap(context->allocation, block_offset) != 0){
//                 //afs_debug("bitmap result offset %d  %d \n", block_offset, get_bitmap(context->allocation, block_offset));
// 		//afs_debug("block offset collision\n");
//                 block_offset = block_offset + random_offset(100);
//                 if(block_offset > context->list_len){
//                     block_offset = random_offset(100);
//                 }
//             }
//             //afs_debug("list_length: %d\n", context->list_len);
// 	    afs_debug("block offset: %d\n", block_offset);
//             map_block[j].tuples[k].block_num = context->block_list[block_offset];
//             set_bitmap(context->allocation, block_offset);
// 	    block_offset += random_offset(100);
//             //write the checksum for each individual data block
//         }
// 	afs_debug("Map block written\n");
//     }
//     afs_debug("map blocks formatted in memory\n");
//     afs_debug("Entries: %d", entries);
//     afs_debug("Entry size: %d", entry_size);
//     //afs_debug("Blocks: %d", blocks);
//     afs_debug("Entries per block: %d", entries_per_block);

//     //rewrite to handle the blocks correctly
//     for(i = 0; i < blocks; i++){
//         io.io_sector = (context->block_list[map_offsets[i]] * context->sectors_per_block) + context->data_start_off;
//         if(i < (blocks - 1)){
//             afs_debug("entry size %lu\n", entries_per_block * sizeof(struct afs_map_entry));
//             memcpy(data, &map_block[i * entries_per_block], entries_per_block * sizeof(struct afs_map_entry));
//             memcpy(data + (entry_size_32 * entries_per_block), &map_offsets[i+1], sizeof(u32));
//         }else{
//             afs_debug("last block\n");
//             memcpy(data, &map_block[i * entries_per_block], (entries % entries_per_block) * sizeof(struct afs_map_entry));
//         }
//         ret = afs_blkdev_io(&io, MKS_IO_WRITE);
//         if(ret){
//             afs_alert("Error when writing map block {%d}\n", i);
//         }
//         afs_debug("block written\n");
//     }
//     __free_page(page);
//     return map_block;
// }

// //retrieve the matryoshka map from disk and save into memory in order to speed up some stuff.
// struct afs_map_entry* retrieve_map(u32 entries, struct afs_fs_context *context, struct block_device *device, struct afs_super *super){
//     //calculate how many blocks are needed to store the map
//     int i = 0;
//     int tuple_size = 8;
//     u32 entry_size = (32 + tuple_size*(32) + 128)/8;
//     u32 block_size = context->sectors_per_block * 512;
//     u32 entries_per_block = block_size / entry_size;
//     u32 blocks = (entries / entries_per_block) + 1; 
//     //u32 map_offsets[blocks];
//     int ret;
//     void *data;
//     const u32 read_length = 1 << PAGE_SHIFT;
//     struct page *page;
//     u32 current_block = super->afs_map_start;
//     struct afs_io io = {
//         .bdev = device,
//         .io_size = read_length
//     };
//     struct afs_map_entry *map_blocks;
//     struct afs_map_entry *block;
//     int entry_size_32 = sizeof(struct afs_map_entry) / 4;

//     if(block_size - (entry_size * entries_per_block) < entry_size){
//         entries_per_block -= 1;
//         blocks = (entries / entries_per_block) + 1;
//     }

//     page = alloc_pages(GFP_KERNEL, (unsigned int)bsr((entry_size * entries)/512));
//     data = page_address(page);
//     io.io_page = page;

//     map_blocks = kmalloc(entries * sizeof(struct afs_map_entry), GFP_KERNEL);
//     //execute a for loop over every logical block number
//     for(i = 0; i<blocks; i++){
//         if(block == 0){
//             afs_debug("retrieving map block %d\n", i);
//             io.io_sector = (current_block * context->sectors_per_block) + context->data_start_off;
//             ret = afs_blkdev_io(&io, MKS_IO_READ);
//             if(ret){
//                 afs_alert("Error when reading map block {%d}\n", i);
//             }
//             memcpy(&map_blocks[i * entries_per_block], data, entries_per_block * sizeof(struct afs_map_entry));
//             block = data;
//             set_bitmap(context->allocation, current_block);
//             memcpy(&current_block, data + (entry_size_32 * entries_per_block), sizeof(u32));
//         }
//     }
//     afs_debug("Finished retrieving map");
//     __free_page(page);
//     return map_blocks;
// }

// //get a pointer to a new superblock
// //TODO: add a field for a tuple of the other superblock block offsets
// struct afs_super * generate_superblock(unsigned char *digest, u64 afs_size, u8 ecc_scheme, u8 secret_split_type, u32 afs_map_start){
//     struct afs_super *super;
//     super = kmalloc(sizeof(struct afs_super), GFP_KERNEL);
//     memcpy(super->hash, (void*)digest ,32);
//     super->afs_size = afs_size;
//     super->ecc_scheme = ecc_scheme;
//     super->secret_split_type = secret_split_type;
//     super->afs_map_start = afs_map_start;
//     return super;
// }

// //write the superblock to the disk in a set number of locations
// int write_new_superblock(struct afs_super *super, int duplicates, unsigned char *digest, struct afs_fs_context *context, struct block_device *device){
//     //u32 location[duplicates];
//     int i;
//     struct page *page;
//     const u32 read_length = 1 << PAGE_SHIFT;
//     void *data;
//     int ret;
//     struct afs_io io = {
//         .bdev = device,
//         .io_sector = (context->block_list[0]*context->sectors_per_block) + context->data_start_off,
//         .io_size = read_length  
//     };
//     page = alloc_page(GFP_KERNEL);
//     if (IS_ERR(page)) {
//         ret = PTR_ERR(page);
//         afs_alert("alloc_page failure {%d}\n", ret);
//         return ret;
//     }
//     data = page_address(page);
//     io.io_page = page;
//     afs_debug("context block %p\n", context->block_list);
//     //compute the hash 8 times and populate  the requisite array, use modulo to determine block offsets
//     //this is nondeterministic, must find a more reliable way to do it over the search space
//     //slightly different superblock for each copy on the disk
//     afs_debug("superblock %p\n", super);
//     memcpy(data, super, sizeof(struct afs_super));
//     //write duplicate number of times to those locations on the disk
//     for(i = 0; i < duplicates; i++){
//         ret = afs_blkdev_io(&io, MKS_IO_WRITE);
//         if(ret){
//             afs_alert("Error when writing superblock copy {%d}\n", i);
//             return ret;
//         }
//         afs_alert("Superblock written {%d}\n", i);
//         set_bitmap(context->allocation, 0);
//     }
//     __free_page(page);
//     return 0;
// }

// //retrieve the superblock from hashes of the passphrase
// struct afs_super* retrieve_superblock(int duplicates, unsigned char *digest, struct afs_fs_context *context, struct block_device *device){
//     int i;
//     const u32 read_length = 1 << PAGE_SHIFT;
//     struct page *page;
//     struct afs_super *super = kmalloc(sizeof(struct afs_super), GFP_KERNEL);
//     void *data;
//     int ret;
//     struct afs_io io = {
//         .bdev = device,
//         .io_sector = (context->block_list[0]*context->sectors_per_block)+ context->data_start_off,
//         .io_size = read_length  
//     };

//     page = alloc_page(GFP_KERNEL);
//     if (IS_ERR(page)) {
//         ret = PTR_ERR(page);
//         afs_alert("alloc_page failure {%d}\n", ret);
//         goto error;
//     }
//     data = page_address(page);
//     io.io_page = page;
//     //compute the first hash and check the location on the disk, if the prefix matches the specific hash then we are good.

//     //check locations
//     for(i = 0; i < duplicates; i++){
// 	//put something here to repair the superblock when it is written to the disk
// 	//if we know that the block is here we can break out of the loop and return.
//         ret = afs_blkdev_io(&io, MKS_IO_READ);
//         if (ret) {
//             afs_alert("afs_blkdev_io failure, Could not read superblock {%d}\n", ret);
//             goto error;
//         }
//         memcpy(super, data, sizeof(struct afs_super));
//         afs_debug("Superblock %p\n", super);
//         afs_debug("Hash %d\n", super->hash[0]);
//         if(super->hash[0] == digest[0]){
//             afs_debug("Superblock copy {%d} found\n", i);
//             set_bitmap(context->allocation, 0);
//             break;
// 	    }
//         afs_debug("Superblock copy {%d} not found\n", i);
//     }
//     if(i == duplicates){
//         afs_alert("superblock not found, either overwritten or no instance ever existed.\n");
//         goto error;
//     }
//     __free_page(page);
//     return super;
// error:
//     return NULL;
// }

// //rewrite new copies of the superblock to bring us up to spec
// int superblock_repair(struct afs_fs_context *context, struct block_device *device){
//     return 0;
// }

// //run repair cycle on the matryoshka map
// int map_repair(struct afs_fs_context *context, struct block_device *device){
//     return 0;
// }

// //takes in the matryoshak map and the free list of blocks, then returns a physical block tuple based on logical block number
// int find_physical_block(struct afs_fs_context *context, struct block_device *device){
//     return 0;
// }


/**
 * Perform a reverse bit scan for an unsigned long.
 */
inline unsigned long
bsr(unsigned long n)
{
	__asm__("bsr %1,%0" : "=r" (n) : "rm" (n));
	return n;
}