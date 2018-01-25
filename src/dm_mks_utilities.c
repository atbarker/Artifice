/**
 * Basic utility system for miscellaneous functions.
 * 
 * Author: Yash Gupta <ygupta@ucsc.edu>
 * Copyright: UC Santa Cruz, SSRC
 */
#include <dm_mks_utilities.h>
#include <linux/errno.h>

/**
 * Used as a callback function to signify the completion of
 * an IO transfer to a block device.
 * 
 * @param   bio     The bio which has completed.
 */
void 
mks_blkdev_callback(struct bio *bio)
{
    struct completion *event = bio->bi_private;

    mks_debug("completed event: %p\n", event);
    complete(event);
}

/**
 * A utility function to conveniently read sectors from a 
 * specified block device. It masks away the asynchronous 
 * nature of the kernel to provide pseudo-synchronous 
 * kernel block IO.
 * 
 * @param   bdev    Block device to read from.
 * @param   dest    The page to write data from block device into.
 * @param   sector  Sector to start reading block device from.
 * @param   size    Size of the read from block device.
 * 
 * @return  0       Successfully read the sector.
 * @return  <0      Error.
 */
void
mks_read_blkdev(struct block_device *bdev, struct page *dest, sector_t sector, u32 size)
{
    const int page_offset = 0;

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
    if (!bio) {
        mks_alert("bio_alloc failure\n");
    }
    bio->bi_bdev = bdev;
    bio->bi_iter.bi_sector = sector;
    bio->bi_private = &event;
    bio->bi_end_io = mks_blkdev_callback;
    bio_add_page(bio, dest, size, page_offset);

    bio->bi_opf |= REQ_OP_READ;
    bio->bi_opf |= REQ_SYNC;
    submit_bio(bio);

    wait_for_completion(&event);
}

void 
mks_write_blkdev(struct block_device *bdev, struct page *src, sector_t sector, u32 size)
{

}