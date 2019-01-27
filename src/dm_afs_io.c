/**
 * Author: Yash Gupta <ygupta@ucsc.edu>, Austen Barker <atbarker@ucsc.edu>
 * Copyright: UC Santa Cruz, SSRC
 */
#include <dm_afs.h>
#include <dm_afs_io.h>
#include <dm_afs_modules.h>

/**
 * Callback function to signify the completion 
 * of an IO transfer to a block device.
 */
static void
__callback_blkdev_io(struct bio *bio)
{
    struct completion *event = bio->bi_private;

    //afs_debug("completed event: %p\n", event);
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
    // bio->bi_opf |= REQ_SYNC;
    // TODO: Perhaps make ASYNC for performance considerations?

    bio_set_dev(bio, request->bdev);
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
 * Read a single page.
 */
int
read_page(void *page, struct block_device *bdev, uint32_t block_num, bool used_vmalloc)
{
    struct afs_io request;
    struct page *page_structure;
    uint64_t sector_num;
    int ret;

    // Make sure page is aligned.
    afs_assert_action(!((uint64_t)page & (AFS_BLOCK_SIZE - 1)), ret = -EINVAL, done, "page is not aligned [%d]", ret);

    // Acquire page structure and sector offset.
    page_structure = (used_vmalloc) ? vmalloc_to_page(page) : virt_to_page(page);
    sector_num = (block_num * AFS_BLOCK_SIZE) / AFS_SECTOR_SIZE;

    // Build the request.
    request.bdev = bdev;
    request.io_page = page_structure;
    request.io_sector = sector_num;
    request.io_size = AFS_BLOCK_SIZE;
    request.type = IO_READ;

    ret = afs_blkdev_io(&request);
    afs_assert(!ret, done, "error in reading block device [%d]", ret);

done:
    return ret;
}

/**
 * Write a single page.
 */
int
write_page(const void *page, struct block_device *bdev, uint32_t block_num, bool used_vmalloc)
{
    struct afs_io request;
    struct page *page_structure;
    uint64_t sector_num;
    int ret;

    // Make sure page is aligned.
    afs_assert_action(!((uint64_t)page & (AFS_BLOCK_SIZE - 1)), ret = -EINVAL, done, "page is not aligned [%d]", ret);

    // Acquire page structure and sector offset.
    page_structure = (used_vmalloc) ? vmalloc_to_page(page) : virt_to_page(page);
    sector_num = (block_num * AFS_BLOCK_SIZE) / AFS_SECTOR_SIZE;

    // Build the request.
    request.bdev = bdev;
    request.io_page = page_structure;
    request.io_sector = sector_num;
    request.io_size = AFS_BLOCK_SIZE;
    request.type = IO_WRITE;

    ret = afs_blkdev_io(&request);
    afs_assert(!ret, done, "error in writing block device [%d]", ret);

done:
    return ret;
}