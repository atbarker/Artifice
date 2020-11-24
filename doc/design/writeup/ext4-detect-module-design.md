# ext4 Detection Module Design
There are five main routines performed by the ext4 detection module:
1. Parsing the superblock
2. Summarizing the partition
3. Reading group descriptors
4. Reading and processing bitmaps
5. Accumulating the free blocks




## Parsing the Superblock
The first step in detecting an ext4 partition is reading the superblock. The superblock for ext4 is set to be `1024 bytes`, or `1KB`. In the superblock is a field named `s_magic`, which indicates ext4's unique magic number. This magic number is what is used to verify whether or not the filesystem the Artifice instance is hiding in is ext4.

The superblock also contains another field of mention: `s_feature_incompat`, a `32-bit` integer whose bits are used as flags for ext4's incompatible feature set. We use logical AND to AND `s_feature_incompat` and `EXT4_INCOMPAT_64BIT`, the later of which is an enumeration of `0x80`, whose bit index indicates whether or not the partition is `32-bit` or `64-bit` ext4.



## Summarizing the Partition
After verifying the partition as ext4, we need to save some information about the partition that we can pass around to other functions to process the partition. This information includes:
- Location of first data block - `first_data_block`
- Number of reserved group descriptor table blocks - `reserved_gdt_blocks`
- Number of free blocks - `free_block_count`
- Number of blocks - `block_count`
- Size of a block - `blk_size`
- Number of blocks per group - `blks_per_grp`
- Number of group descriptors - `num_grp_descs`
- Size of a cluster `cluster_size`
- Flag for sparse redundant superblock copies - `is_sparse_super`
- Flag for if the partition is `32-bit` or `64-bit`

All of this information goes into `struct ext4_disk`. As alluded to above, a populated instance of `struct ext4_disk` will be passed around to other functions to aid in finding free blocks in the partition. While populating `struct ext4_disk`, certain fields will be processed, depending on whether or not the partition is `32-bit` or `64-bit`. In the original design of ext4, certain fields, such as the number of free blocks, were split into high and low order bits. For example, the number of free blocks in the partition would be split into two `32-bit` variables: `s_free_blocks_count_lo` and `s_free_blocks_count_hi`. If the partition is `32-bit` the number of free blocks is set to `s_free_blocks_count_lo`. Otherwise, the partition must be `64-bit` and the number of free blocks is obtained by combining the low and hi order bits into a single `64-bit` unsigned integer.



## Reading Block Group Descriptors
A block group descriptor, or group descriptor, in ext4 contains the information for where to find the block bitmap for its corresponding block group. On a `32-bit` ext4 partition, a single group descriptor's block bitmap represents up to `32768` blocks. Why? Each block is `4096 bytes`, or `4KB`. One byte means `8` bits. Thus, the number of blocks a block bitmap represents is `4096` $*$ ` 8`, or `32768`.  This routine contains the bulk of the work needed to map out the free blocks on an ext4 partition.

#### Calculating Group Descriptors per Block
First, we need to calculate how many group descriptors there are per block in the partition. From `struct ext4_disk`, we have the size of a block (`blk_sz`), and the size of a group descriptor (`grp_desc_sz`). From this we know that the number of group descriptors per block, `num_gds_per_block`,  is simply `blk_size` $/$ `grp_desc_sz`. 

#### Calculating the Number of Group Descriptor Blocks
We now need to calculate the number of group descriptor blocks. What is meant by a group descriptor block is a block that is occupied by at least one group descriptor. We need to calculate this since we need to know how many blocks of we need to read to account for all group descriptors. The calculation is done in the following manner: `gd_blks` $=$ $($`num_grp_descs` * `grp_desc_sz`$)$ / `blk_sz`. Similarly, we need to calculate how many group descriptors are left, if any, in a block that isn't entirely filled with group descriptors: `rem_blk_sz` $=$ $($`num_grp_descs` * `grp_desc_sz`$)$ % `blk_sz`. The calculation for `rem_blk_size` actually just calculates the size in bytes of the remaining group descriptors. To get the number of group descriptors: `rem_gds` $=$ `rem_blk_sz` / `grp_desc_sz`. 

#### Reading The Group Descriptors
Reading the group descriptors requires three loops. The first loop simply reads in `gd_blks` number of blocks. The second loop is nested inside the first, and iterates `num_gds_per_blk` times, effectively parsing each block into separate group descriptors. The byte boundaries of the start and end of each group descriptor is $($`grp_desc_sz` * `j`$)$ and $($`grp_desc_sz` * `j`) $+$ `grp_desc_sz` respectively, where `j` signifies the `j`-th group descriptor in the current read block. The offset for the block to read is incremented by sectors. Sectors are typically `512 bytes`, coupled with the fact that a block is `4KB`, we can see that the sector offset to read blocks is incremented `8` at a time, which has been defined as `AFS_SECTORS_PER_BLOCK`.



## Reading and Processing Block Bitmaps

For each group descriptor that is read in, we read and process its corresponding block bitmap. The location of a group descriptor's block bitmap in a `32-bit` partition is given by the field: `bg_block_bitmap_lo`. In a `64-bit` partition, the location is obtained by combining `bg_block_bitmap_lo` and `bg_block_bitmap_hi` into a single `64-bit` integer. To read the block at this location, we must multiply the block location by the number of sectors per block, given by `AFS_SECTORS_PER_BLOCK`. The block is read into a buffer, which we copy the bytes of into a `bit_vector`. We then need to further process the bits in the `bit_vector`. One of the flags mentioned earlier saved into our `ext4_disk` summary was `is_sparse_super`. If this flag is set, the current ext4 partition features redundant superblock copies in blocks with indices of either 0, or powers of 1, 3, 5, and 7. However, ext4 for some reason doesn't set these blocks as in use, so we have to do so ourselves by setting the bits corresponding to the blocks containing the superblock copies. 




## Accumulating Free Blocks

In each `bit_vector`, the indices that contain a 0, which indicates a free block, are added to the Artifice's `block_list`, which is a list of free block indices. The number of free blocks should always match exactly the amount indicated by the disk summary field: `free_block_count`. This field is how we can verify whether or not the ext4 partition was parsed correctly or not. As of now (October 7th, 2019), Artifice doesn't currently support `64-bit` ext4 partitions, so any free block indices greater than `0xFFFFFFFF` (the max `32-bit` unsigned integer value) are ignored.



## Optimizations

The routines above have been written to reflect the algorithmic choices that will eventually be refactored into the existing code. The current implementation of this ext4 detection module was written with debugging in mind, leading to inefficient memory usage. Group descriptors are saved into an array and each bitmap into a `bit_vector`. It is far more efficient and optimal to simply perform the needed calculations on each read group descriptor and move on, and simply iterate through block bitmaps and add free blocks, instead of copying them into a `bit_vector`. 
