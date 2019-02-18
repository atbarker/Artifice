#include "dm_afs_entropy.h"
#include <linux/random.h>
#include <linux/fs.h>
#include <linux/hashtable.h>
#include <linux/hash.h>
#include <linux/syscalls.h>
#include <asm/uaccess.h>

//redefine our own hash table add to use the hash_64_generic function for 64 bit values
//gave it protection via rcu just in case
#define hash_add_64(hashtable, node, key)						\
    hlist_add_head_rcu(node, &hashtable[hash_64_generic(key, HASH_BITS(hashtable))])

#define BLOCK_LENGTH 4096

DEFINE_HASHTABLE(HASH_TABLE_NAME, HASH_TABLE_ORDER);

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
    uint64_t filename_hash = 0;
    struct entropy_hash_entry *entry = kmalloc(sizeof(struct entropy_hash_entry), GFP_KERNEL);

    //TODO, hash the filename to a 64bit value
    
    entry->key = filename_hash;
    entry->filename = filename;
    //entry.hash_list = NULL;

    hash_add_64(HASH_TABLE_NAME, &entry->hash_list, entry->key);
    return 0;
}

//sorcery
//recursive list, ls $(find <path> -not -path '*/\.*' -type f)
//iterate_dir
//hook into sys_getdents
//just do the sys_call/get_fs/set_fs dance
void scan_directory(char* directory_name, char** file_list){
    struct file *file;
    loff_t pos = 0;
    //This code block is blasphemy, should only be used when no other option works.
    /*int fd;
    struct linux_dirent __user *dirents;
    int count = 0;
    mm_segment_t old_fs = get_fs();
    set_fs(KERNEL_DS);
    fd = sys_open(directory_name, O_RDONLY, 0);
    if (fd >= 0){
        sys_getdents(fd, dirents, count); 
        sys_close(fd);
    }
    set_fs(old_fs);*/
}

void build_entropy_ht(char* directory_name){
    int i, file_count = 0;
    char** filename_list = NULL;

    //initialize hash table
    hash_init(HASH_TABLE_NAME);

    for(i = 0; i < file_count; i++){
        insert_entropy_ht(filename_list[i]);
    }
}

void cleanup_entropy_ht(void){
    int bucket;
    struct entropy_hash_entry *entry;

    hash_for_each_rcu(HASH_TABLE_NAME, bucket, entry, hash_list){
	//the filename will have been malloc'd elsewhere
	kfree(entry->filename);
	kfree(entry);
    }
}

void allocate_entropy(uint64_t filename_hash, uint32_t block_pointer){

}

char* retrieve_filename(uint64_t filename_hash){
    struct entropy_hash_entry *entry;
    hash_for_each_possible(HASH_TABLE_NAME, entry, hash_list, filename_hash){
	if(filename_hash == entry->key){
            return entry->filename;
	}
    }
    return NULL;
}

int read_entropy(uint64_t filename_hash, uint32_t block_pointer, uint8_t* block){
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
    file_close(file);
    return ret;
}
