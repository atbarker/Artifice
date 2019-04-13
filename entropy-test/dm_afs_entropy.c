#include "dm_afs_entropy.h"
#include <linux/random.h>
#include <linux/fs.h>
#include <linux/hashtable.h>
#include <linux/hash.h>
#include <linux/syscalls.h>
#include <linux/string.h>
#include <linux/path.h>
#include <linux/namei.h>
#include <asm/uaccess.h>

//redefine our own hash table add to use the hash_64_generic function for 64 bit values
//gave it protection via rcu just in case multiple things try to access it, although it should be read only
#define hash_add_64(hashtable, node, key)						\
    hlist_add_head_rcu(node, &hashtable[hash_64_generic(key, HASH_BITS(hashtable))])

#define BLOCK_LENGTH 4096
#define FILE_LIST_SIZE 1024

DEFINE_HASHTABLE(HASH_TABLE_NAME, HASH_TABLE_ORDER);

//need to get rid of the global context
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
 * vfs_llseek() causes a seg fault
 * vfs_stat() seems to work
 */
int insert_entropy_ht(char *filename){
    uint64_t filename_hash = 0;
    struct kstat stat;
    struct path p;
    struct entropy_hash_entry *entry = kmalloc(sizeof(struct entropy_hash_entry), GFP_KERNEL);

    if(!filename){
        printk(KERN_INFO "Filename Null\n");
	return -1;
    }
    filename_hash = djb2_hash(filename);
    
    entry->key = filename_hash;
    entry->filename = filename;

    kern_path(filename, LOOKUP_FOLLOW, &p);
    vfs_getattr(&p, &stat, STATX_ALL, KSTAT_QUERY_FLAGS);
    //printk(KERN_INFO "File size %s, %llu\n", filename, stat.size);
    entry->file_size = stat.size;

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
        size_t full_path_size = name_length + ent_context.directory_name_length + 2;
        ent_context.file_list[ent_context.number_of_files] = kmalloc(sizeof(char) * (full_path_size), GFP_KERNEL);
	strlcpy(ent_context.file_list[ent_context.number_of_files], ent_context.directory_name, full_path_size);
	strlcat(ent_context.file_list[ent_context.number_of_files], "/", full_path_size);
	strlcat(ent_context.file_list[ent_context.number_of_files], name, full_path_size);
	insert_entropy_ht(ent_context.file_list[ent_context.number_of_files]);
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
void build_entropy_ht(char* directory_name, size_t name_length){
    int i;

    //TODO experiment with setting it to this size
    ent_context.file_list = kmalloc(sizeof(char*) * FILE_LIST_SIZE, GFP_KERNEL);
    ent_context.directory_name = kmalloc(sizeof(char) * name_length + 1, GFP_KERNEL);
    strlcpy(ent_context.directory_name, directory_name, name_length + 1);
    ent_context.directory_name_length = name_length;    
    
    //initialize hash table
    hash_init(HASH_TABLE_NAME);

    scan_directory(directory_name);
    //printk(KERN_INFO "number of files %d\n", ent_context.number_of_files);
    for(i = 0; i < ent_context.number_of_files; i++){
	if(ent_context.file_list[i]){
	    //TODO figure out why inserting here causes a slab free error
            //insert_entropy_ht(ent_context.file_list[i]);
	}else{
            printk(KERN_INFO "Filename null %d\n", i);
	}
    }
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
    kfree(ent_context.directory_name);

    hash_for_each_rcu(HASH_TABLE_NAME, bucket, entry, hash_list){
	//TODO Check here for memory leaks
	//kfree(entry->filename);
	kfree(entry);
    }
}

/**
 * Retrieve a full file name from the hash table
 */
struct entropy_hash_entry*  retrieve_file_data(uint64_t filename_hash){
    struct entropy_hash_entry *entry;
    hash_for_each_possible(HASH_TABLE_NAME, entry, hash_list, filename_hash){
        if(filename_hash == entry->key){
            return entry;
        }
    }
    return NULL;
}

/**
 * Allocate a random entropy block from the file list for use in an encoding tuple
 */
void allocate_entropy(uint64_t filename_hash, uint32_t block_pointer, uint8_t* entropy_block){
    int file_index, block_index;
    struct entropy_hash_entry *entry = NULL;

    //get random numbers to pick a random entropy block out of our pool
    get_random_bytes(&file_index, sizeof(int));
    get_random_bytes(&block_index, sizeof(int));

    //calculate the index of our random file and retrieve data on that file
    file_index = file_index % ent_context.number_of_files;
    entry = retrieve_file_data(djb2_hash(ent_context.file_list[file_index]));

    //calculate the block index within the bounds of file size and retrieve data
    block_index = block_index % entry->file_size;
    read_entropy(entry->key, block_index, entropy_block); 
}

/**
 * Read a specific entropy block with assistance from the filename
 */
int read_entropy(uint64_t filename_hash, uint32_t block_pointer, uint8_t* block){
    int ret;
    struct entropy_hash_entry* entry = NULL;
    struct file* file = NULL;
    loff_t offset = block_pointer * BLOCK_LENGTH;

    entry = retrieve_file_data(filename_hash);

    if(entry != NULL){
        file = file_open(entry->filename, O_RDONLY, 0);
        ret = vfs_read(file, block, BLOCK_LENGTH, &offset);
        file_close(file);
    }else{
        printk(KERN_INFO "Could not read hash table entry\n");
	ret = -1;
    }

    return ret;
}
