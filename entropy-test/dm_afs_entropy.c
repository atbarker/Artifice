#include "dm_afs_entropy.h"
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/random.h>
#include <linux/fs.h>

//TODO change this to the actual hash length, we should only use one, fine to hard code
#define HASH_LENGTH 12
#define BLOCK_LENGTH 4096

struct file* file_open(char* path, int flags, int rights){
    struct file *filp = NULL;
    //mm_segment_t oldfs;

    //oldfs = get_fs();
    //set_fs(get_ds());
    filp = filp_open(path, flags, rights);
    //set_fs(oldfs);
    return filp;
}

void file_close(struct file* file){
    filp_close(file, NULL);
}

int insert_entropy_ht(char *filename){
    return 0;
}

//TODO, go over this bit with Yash
void build_entropy_ht(char* directory_name){

}

void allocate_entropy(uint8_t* filename_hash, uint32_t block_pointer){

}

char* retrieve_filename(uint8_t* filename_hash){
    return NULL;
}

int read_entropy(uint8_t* filename_hash, uint32_t block_pointer, uint8_t* block){
    //mm_segment_t oldfs;
    int ret;
    char* filename;
    struct file* file = NULL;
    loff_t offset = block_pointer * BLOCK_LENGTH;

    filename = retrieve_filename(filename_hash);

    file = file_open(filename, O_RDONLY, 0);

    //oldfs = get_fs();
    //set_fs(get_ds());

    ret = vfs_read(file, block, BLOCK_LENGTH, &offset);

    //set_fs(oldfs);
    return ret;
}
