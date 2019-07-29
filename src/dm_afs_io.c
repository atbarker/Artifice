/**
 * Author: Yash Gupta <ygupta@ucsc.edu>, Austen Barker <atbarker@ucsc.edu>
 * Copyright: UC Santa Cruz, SSRC
 */
#include <dm_afs.h>
#include <dm_afs_io.h>
#include <dm_afs_modules.h>
#include <dm_afs_format.h>
#include <linux/slab.h>
#include <linux/delay.h>



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

    bio = bio_alloc(GFP_NOIO, 1);
    afs_action(!IS_ERR(bio), ret = PTR_ERR(bio), alloc_err, "could not allocate bio [%d]", ret);

    switch (request->type) {
    case IO_READ:
        bio->bi_opf |= REQ_OP_READ;
        break;

    case IO_WRITE:
        bio->bi_opf |= REQ_OP_WRITE;
        break;

    default:
        afs_action(0, ret = -EINVAL, invalid_type, "invalid IO type [%d]", request->type);
    }

    bio_set_dev(bio, request->bdev);
    bio->bi_iter.bi_sector = request->io_sector;
    bio_add_page(bio, request->io_page, request->io_size, page_offset);

    submit_bio_wait(bio);
    kfree(bio);
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
read_page(void *page, struct block_device *bdev, uint32_t block_num, uint32_t sector_offset, bool used_vmalloc)
{
    struct afs_io request;
    struct page *page_structure;
    uint64_t sector_num;
    int ret;

    // Make sure page is aligned.
    afs_action(!((uint64_t)page & (AFS_BLOCK_SIZE - 1)), ret = -EINVAL, done, "page is not aligned [%d]", ret);

    // Acquire page structure and sector offset.
    page_structure = (used_vmalloc) ? vmalloc_to_page(page) : virt_to_page(page);
    sector_num = ((block_num * AFS_BLOCK_SIZE) / AFS_SECTOR_SIZE) + sector_offset;

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

int
read_pages(void **pages, struct block_device *bdev, uint32_t *block_nums, uint32_t sector_offset, bool used_vmalloc, size_t num_pages){
    uint64_t sector_num;
    const int page_offset = 0;
    int i = 0;
    int ret = 0;
    struct bio **bio = NULL;
    struct bio *iterator = NULL;
    struct bio_list req_list;

    bio = kmalloc(sizeof(struct bio *) * num_pages, GFP_KERNEL);
    bio_list_init(&req_list);

    for(i = 0; i < num_pages; i++){
	struct page *page_structure;
        //struct bio *bio = NULL;

        bio[i] = bio_alloc(GFP_NOIO, 1);
        afs_action(!IS_ERR(bio[i]), ret = PTR_ERR(bio[i]), done, "could not allocate bio [%d]", ret);

        // Make sure page is aligned.
        afs_action(!((uint64_t)pages[i] & (AFS_BLOCK_SIZE - 1)), ret = -EINVAL, done, "page is not aligned [%d]", ret);

        // Acquire page structure and sector offset.
        page_structure = (used_vmalloc) ? vmalloc_to_page(pages[i]) : virt_to_page(pages[i]);
        sector_num = ((block_nums[i] * AFS_BLOCK_SIZE) / AFS_SECTOR_SIZE) + sector_offset;

        bio[i]->bi_opf |= REQ_OP_READ;
        bio_set_dev(bio[i], bdev);
        bio[i]->bi_iter.bi_sector = sector_num;
        bio_add_page(bio[i], page_structure, AFS_BLOCK_SIZE, page_offset);

	bio_list_add(&req_list, bio[i]);
        //submit_bio_wait(bio[i]);
        //bio_put(bio[i]);
	//kfree(bio[i]);
    }
    submit_bio_wait(req_list.head);
    //bio_list_for_each(iterator, &req_list){
    //    bio_put(iterator);
    //}
done:
    kfree(bio);
    //if(bio[0]){
    //        afs_debug("bio size %p %ld", bio[0], sizeof(struct bio));
    //}
    return ret;
}

/**
 * Write a single page.
 */
int
write_page(const void *page, struct block_device *bdev, uint32_t block_num, uint32_t sector_offset, bool used_vmalloc)
{
    struct afs_io request;
    struct page *page_structure;
    uint64_t sector_num;
    int ret;

    // Make sure page is aligned.
    afs_action(!((uint64_t)page & (AFS_BLOCK_SIZE - 1)), ret = -EINVAL, done, "page is not aligned [%d]", ret);

    // Acquire page structure and sector offset.
    page_structure = (used_vmalloc) ? vmalloc_to_page(page) : virt_to_page(page);
    sector_num = ((block_num * AFS_BLOCK_SIZE) / AFS_SECTOR_SIZE)  + sector_offset;

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

int
write_pages(const void **pages, struct block_device *bdev, uint32_t *block_nums, uint32_t sector_offset, bool used_vmalloc, size_t num_pages){
    uint64_t sector_num;
    int ret = 0;
    int i = 0;
    const int page_offset = 0;
    struct bio **bio = NULL;
    struct bio *iterator = NULL;
    struct bio_list req_list;


    bio = kmalloc(sizeof(struct bio *) * num_pages, GFP_KERNEL);
    bio_list_init(&req_list);

    for(i = 0; i < num_pages; i++){
        struct page *page_structure;

        bio[i] = bio_alloc(GFP_NOIO, 1);
        afs_action(!IS_ERR(bio[i]), ret = PTR_ERR(bio[i]), done, "could not allocate bio [%d]", ret);

        // Make sure page is aligned.
        afs_action(!((uint64_t)pages[i] & (AFS_BLOCK_SIZE - 1)), ret = -EINVAL, done, "page is not aligned [%d]", ret);

        // Acquire page structure and sector offset.
        page_structure = (used_vmalloc) ? vmalloc_to_page(pages[i]) : virt_to_page(pages[i]);
        sector_num = ((block_nums[i] * AFS_BLOCK_SIZE) / AFS_SECTOR_SIZE) + sector_offset;

        bio[i]->bi_opf |= REQ_OP_WRITE;
        bio_set_dev(bio[i], bdev);
        bio[i]->bi_iter.bi_sector = sector_num;
        bio_add_page(bio[i], page_structure, AFS_BLOCK_SIZE, page_offset);

	//bio_list_add(&req_list, bio[i]);
        submit_bio_wait(bio[i]);
	bio_put(bio[i]);
    }
    //submit_bio_wait(req_list.head);
    //bio_list_for_each(iterator, &req_list){
    //    bio_put(iterator);
    //}
done:
    kfree(bio);
    return ret;
}
