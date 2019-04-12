#include "dm_afs_entropy.h"
#include <linux/random.h>
#include <linux/fs.h>
#include <linux/hashtable.h>
#include <linux/hash.h>
#include <linux/syscalls.h>
#include <linux/string.h>
#include <asm/uaccess.h>

//redefine our own hash table add to use the hash_64_generic function for 64 bit values
//gave it protection via rcu just in case multiple things try to access it, although it should be read only
#define hash_add_64(hashtable, node, key)						\
    hlist_add_head_rcu(node, &hashtable[hash_64_generic(key, HASH_BITS(hashtable))])

#define BLOCK_LENGTH 4096
#define FILE_LIST_SIZE 1024

DEFINE_HASHTABLE(HASH_TABLE_NAME, HASH_TABLE_ORDER);
static struct entropy_context ent_context = {
    .number_of_files = 0,
};

/**
 * Damn black magic
 * http://www.cse.yorku.ca/~oz/hash.html
 */
uint64_t djb2_hash(unsigned char *str){
    unsigned long hash = 5381;
    int c;

    while((c = *str++)){
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

/**
 * Helper function for opening a file in the kernel
 */
struct file* file_open(char* path, int flags, int rights){
    struct file *filp = NULL;
    filp = filp_open(path, flags, rights);
    return filp;
}

/**
 * Closing a file in the kernel
 */
void file_close(struct file* file){
    filp_close(file, NULL);
}


/**
 * Insert something into the entropy hash table
 */
int insert_entropy_ht(char *filename){
    uint64_t filename_hash = 0;
    struct file* file = NULL;
    loff_t ret = 0;
    struct entropy_hash_entry *entry = kmalloc(sizeof(struct entropy_hash_entry), GFP_KERNEL);

    filename_hash = djb2_hash(filename);
    
    entry->key = filename_hash;
    entry->filename = filename;
    //TODO, determine file size (important for allocation)
    file = file_open(filename, O_RDONLY, 0);

    //should seek to the end of the file
    ret = vfs_llseek(file, 0, SEEK_END);

    file_close(file);
    entry->file_size = ret;
    //entry.hash_list = NULL;

    hash_add_64(HASH_TABLE_NAME, &entry->hash_list, entry->key);
    return 0;
}

/**
 * Filldir function
 * Does the heavy lifting when iterating through a directory
 * Need to make it recursive
 */
static int dm_afs_filldir(struct dir_context *context, const char *name, int name_length, loff_t offset, u64 ino, unsigned d_type){
    if(ent_context.number_of_files < FILE_LIST_SIZE){
	//insert_entropy_ht(name);
        ent_context.file_list[ent_context.number_of_files] = kmalloc(name_length, GFP_KERNEL);
	memcpy(ent_context.file_list[ent_context.number_of_files], name, name_length);
        ent_context.number_of_files++;
    }
    return 0;
}


/**
 * Sorcery
 * This scans a directory and returns a list of the files in that directory
 * It could also be possible hook into the system call sys_getdents()
 */
//recursive list, ls $(find <path> -not -path '*/\.*' -type f)
void scan_directory(char* directory_name){
    struct file *file = NULL;
    struct dir_context context = {
        .actor = dm_afs_filldir,
        .pos = 0		
    };

    file = filp_open(directory_name, O_RDONLY, 0);
    if (file){
        iterate_dir(file, &context);
    }
}


/**
 * Entropy hash table constructor
 */
void build_entropy_ht(char* directory_name){
    int i;

    //TODO experiment with setting it to this size
    ent_context.file_list = kmalloc(sizeof(char*) * FILE_LIST_SIZE, GFP_KERNEL);    
    
    //initialize hash table
    hash_init(HASH_TABLE_NAME);

    scan_directory(directory_name);
    printk(KERN_INFO "number of files %d\n", ent_context.number_of_files);
    //for(i = 0; i < ent_context.number_of_files; i++){
    //    insert_entropy_ht(ent_context.file_list[i]);
    //}
}


/**
 * Entropy hash table destructor
 */
void cleanup_entropy_ht(void){
    int bucket, i;
    struct entropy_hash_entry *entry;

    for(i = 0; i < ent_context.number_of_files; i++){
        kfree(ent_context.file_list[i]);
    } 
    kfree(ent_context.file_list);

    hash_for_each_rcu(HASH_TABLE_NAME, bucket, entry, hash_list){
	//the filename will have been malloc'd elsewhere
	kfree(entry->filename);
	kfree(entry);
    }
}

/**
 * Allocate a random entropy block from the file list for use in an encoding tuple
 */
void allocate_entropy(uint64_t filename_hash, uint32_t block_pointer){

}

/**
 * Retrieve a full file name from the hash table
 */
char* retrieve_filename(uint64_t filename_hash){
    struct entropy_hash_entry *entry;
    hash_for_each_possible(HASH_TABLE_NAME, entry, hash_list, filename_hash){
	if(filename_hash == entry->key){
            return entry->filename;
	}
    }
    return NULL;
}

/**
 * Read a specific entropy block with assistance from the filename
 */
int read_entropy(uint64_t filename_hash, uint32_t block_pointer, uint8_t* block){
    int ret;
    char* filename;
    struct file* file = NULL;
    loff_t offset = block_pointer * BLOCK_LENGTH;

    filename = retrieve_filename(filename_hash);

    file = file_open(filename, O_RDONLY, 0);

    ret = vfs_read(file, block, BLOCK_LENGTH, &offset);

    file_close(file);
    return ret;
}
