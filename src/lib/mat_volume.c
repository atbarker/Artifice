/*
 * Mat_volume.c
 *
 * Read filesystem superblock and file allocation table.
 * Matryoshka file allocation table is the same as standard FAT
 * The Superblock that contains hashes of the other blocks is different, see design for details
 */

#include <linux/errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <linux/gfp.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/kmalloc.h>

#include "mat_util.h"
#include "mat_volume.h"

/* Extended BIOS Parameter Block (non-FAT-32 version) */
struct nonfat32_ebpb {
	u8 physical_drive_num;
	u8 reserved;
	u8 extended_boot_sig;
	le32 volume_id;
	char volume_label[11];
	char fs_type[8];
} __attribute__((packed));

/* Extended BIOS Parameter Block (FAT-32 version) */
struct fat32_ebpb {
	le32 sectors_per_fat;
	le16 drive_description;
	le16 version;
	le32 root_dir_start_cluster;
	le16 fs_info_sector;
	le16 alt_boot_sector;
	u8 reserved[12];
} __attribute__((packed));

/* On-disk format of the FAT boot sector, starting from the very first byte of
 * the device. */
struct fat_boot_sector_disk {
	/* Jump instruction for the bootloader (ignored) */
	u8 jump_insn[3];

	/* Standard boot sector info */
	char oem_name[8];

	/* DOS 2.0 BIOS Parameter Block (13 bytes) */
	le16 bytes_per_sector;
	u8   sectors_per_cluster;
	le16 reserved_sectors;
	u8   num_tables;
	le16 max_root_entries;
	le16 total_sectors;
	u8   media_descriptor;
	le16 sectors_per_fat;

	/* DOS 3.31 BIOS Parameter Block (12 bytes) */
	le16 sectors_per_track;
	le16 num_heads;
	le32 hidden_sectors;
	le32 total_sectors_32;

	/* Extended BIOS Parameter Block (different depending on whether the FAT
	 * version is FAT32 or not) */
	union __attribute__((packed)) {
		struct __attribute__((packed)) {
			struct fat32_ebpb fat32_ebpb;
			struct nonfat32_ebpb nonfat32_ebpb;
		} fat32;
		struct nonfat32_ebpb nonfat32_ebpb;
	} ebpb;
} __attribute__((packed));

/* Read DOS 2.0-compatible "BIOS Parameter Block" (13 bytes) */
static int
fat_read_dos_2_0_bpb(struct fat_volume *vol,
		     const struct fat_boot_sector_disk *boot_sec)
{
	vol->bytes_per_sector = le16_to_cpu(boot_sec->bytes_per_sector);
	vol->sector_order = bsr(vol->bytes_per_sector);

	/* Make sure the sector size in bytes is a power of 2 between 32 and
	 * 4096, inclusive */
	if (!is_power_of_2(vol->bytes_per_sector) ||
	     vol->sector_order < 5 || vol->sector_order > 12)
	{
		fat_error("bytes_per_sector (%hu) is invalid",
			  vol->bytes_per_sector);
		goto out_invalid;
	}

	DEBUG("sector_order = %hu (%hu bytes per sector)",
	      vol->sector_order, vol->bytes_per_sector);

	/* Make sure the sectors per cluster is a power of 2 between 1 and 128,
	 * inclusive */
	vol->sectors_per_cluster = boot_sec->sectors_per_cluster;
	vol->sectors_per_cluster_order = bsr(vol->sectors_per_cluster);

	if (!is_power_of_2(vol->sectors_per_cluster) ||
	    vol->sectors_per_cluster_order > 7)
	{
		fat_error("sectors_per_cluster (%hhu) is invalid",
			  vol->sectors_per_cluster);
		goto out_invalid;
	}
	vol->cluster_order = vol->sector_order + vol->sectors_per_cluster_order;

	DEBUG("cluster_order = %hu (%hhu sectors = %u bytes per cluster)",
	      vol->cluster_order, vol->sectors_per_cluster,
	      1 << vol->cluster_order);

	/* Number of reserved sectors */
	vol->reserved_sectors = le16_to_cpu(boot_sec->reserved_sectors);

	DEBUG("reserved_sectors = %hu", vol->reserved_sectors);

	/* Number of FATs must be 1 or 2 */
	vol->num_tables = boot_sec->num_tables;
	if (vol->num_tables != 1 && vol->num_tables != 2) {
		fat_error("num_tables (%hhu) is invalid", vol->num_tables);
		goto out_invalid;
	}

	DEBUG("num_tables = %hhu", vol->num_tables);

	vol->max_root_entries = le16_to_cpu(boot_sec->max_root_entries);
	if (vol->max_root_entries == 0) {
		/* Max root directory entries is 0: this indicates a FAT32
		 * volume (root directory is stored in ordinary data clusters)
		 * */
		vol->type = FAT_TYPE_FAT32;
		DEBUG("type = FAT_TYPE_FAT32");
	} else {
		/* Max root directory entries is nonzero: round the number up so
		 * that the number of 32-byte directory entries aligns to a
		 * sector boundary. */
		DEBUG("(not FAT 32)");
		u16 root_entries_per_sector = 1 << (vol->sector_order - 5);
		vol->max_root_entries =
			(vol->max_root_entries + (root_entries_per_sector - 1))
					& ~(root_entries_per_sector - 1);
		if (vol->max_root_entries == 0) { /* Overflow */
			fat_error("Invalid number of root entries");
			goto out_invalid;
		}
		DEBUG("max_root_entries = %hu", vol->max_root_entries);
	}

	/* Total logical sectors (2 bytes); if 0 use 4 byte value later */
	vol->total_sectors = le16_to_cpu(boot_sec->total_sectors);

	DEBUG("total_sectors (16-bit) = %u", vol->total_sectors);

	vol->media_descriptor = boot_sec->media_descriptor;

	DEBUG("media_descriptor = %x", vol->media_descriptor);

	vol->sectors_per_fat = le16_to_cpu(boot_sec->sectors_per_fat);

	DEBUG("sectors_per_fat (16-bit) = %u", vol->sectors_per_fat);
	return 0;
out_invalid:
	errno = EINVAL;
	return -1;
}

/* Read DOS 3.31-compatible "BIOS Parameter Block" (12 bytes) */
static int
fat_read_dos_3_31_bpb(struct fat_volume *vol,
		      const struct fat_boot_sector_disk *boot_sec)
{
	vol->sectors_per_track = le16_to_cpu(boot_sec->sectors_per_track);
	vol->num_heads = le16_to_cpu(boot_sec->num_heads);
	vol->hidden_sectors = le32_to_cpu(boot_sec->hidden_sectors);

	DEBUG("sectors_per_track = %hu", vol->sectors_per_track);
	DEBUG("num_heads = %hu", vol->num_heads);
	DEBUG("vol->hidden_sectors = %u", vol->hidden_sectors);

	if (vol->total_sectors == 0) {
		vol->total_sectors = le32_to_cpu(boot_sec->total_sectors_32);
		DEBUG("vol->total_sectors (32-bit) = %u", vol->total_sectors);
	} else {
		DEBUG("Using 16-bit total_sectors");
	}
	return 0;
}

/* Read generic "Extended BIOS Parameter Block" */
static int
fat_read_nonfat32_ebpb(struct fat_volume *vol,
		       const struct nonfat32_ebpb *ebpb)
{
	vol->physical_drive_num = ebpb->physical_drive_num;
	vol->extended_boot_sig = ebpb->extended_boot_sig;
	vol->volume_id = le32_to_cpu(ebpb->volume_id);
	memcpy(vol->volume_label, ebpb->volume_label, sizeof(ebpb->volume_label));
	memcpy(vol->fs_type, ebpb->fs_type, sizeof(ebpb->fs_type));
	remove_trailing_spaces(vol->volume_label);
	remove_trailing_spaces(vol->fs_type);

	DEBUG("physical_drive_num = %hhu", vol->physical_drive_num);
	DEBUG("extended_boot_sig = %hhu", vol->extended_boot_sig);
	DEBUG("volume_id = %u", vol->volume_id);
	DEBUG("volume_label = \"%s\"", vol->volume_label);
	DEBUG("fs_type = \"%s\"", vol->fs_type);
	return 0;
}

/* Read FAT32-specific "Extended BIOS Parameter Block" */
static int
fat_read_fat32_ebpb(struct fat_volume *vol, const struct fat32_ebpb *ebpb)
{
	if (le32_to_cpu(ebpb->sectors_per_fat) != 0) {
		vol->sectors_per_fat = le32_to_cpu(ebpb->sectors_per_fat);
		if ((((size_t)vol->sectors_per_fat <<
			vol->sector_order) >>
			    vol->sector_order) != (size_t)vol->sectors_per_fat)
		{
			/* Size of FAT in bytes does not fit in 'size_t'. */
			fat_error("Unexpectedly high sectors per FAT (%u)",
				  vol->sectors_per_fat);
			goto out_invalid;
		}
	}
	vol->drive_description = le16_to_cpu(ebpb->drive_description);
	vol->version = le16_to_cpu(ebpb->version);
	if (vol->version != 0) {
		fat_error("Unknown filesystem version %hu; refusing to mount", vol->version);
		goto out_invalid;
	}
	vol->root_dir_start_cluster = le32_to_cpu(ebpb->root_dir_start_cluster);
	if (vol->root_dir_start_cluster == 0) {
		fat_error("Invalid starting cluster for root directory");
		goto out_invalid;
	}
	vol->fs_info_sector = le16_to_cpu(ebpb->fs_info_sector);
	vol->alt_boot_sector = le16_to_cpu(ebpb->alt_boot_sector);
	DEBUG("sectors_per_fat (32-bit) = %u", vol->sectors_per_fat);
	DEBUG("drive_description = 0x%hx", vol->drive_description);
	DEBUG("version = 0x%hx", vol->version);
	DEBUG("root_dir_start_cluster = %u", vol->root_dir_start_cluster);
	DEBUG("fs_info_sector = %hu", vol->fs_info_sector);
	DEBUG("alt_boot_sector = %hu", vol->alt_boot_sector);

	if (vol->fs_info_sector == 0xffff)
		vol->fs_info_sector = 0;
	if (vol->fs_info_sector != 0 && vol->sector_order < 9) {
		fat_error("FS Information Sector is present, but "
			  "sector size is less than 512 bytes");
		goto out_invalid;
	}
	return 0;
out_invalid:
	errno = EINVAL;
	return -1;
}

/* Read the data from the FAT filesystem's boot sector, located at the beginning
 * of the file given by the file descriptor @fd, into the `struct fat_volume'
 * @vol.  Return 0 on success; returns -1 and sets errno on failure. */
static int
fat_read_boot_sector(struct fat_volume *vol, int fd)
{
	u8 buf[512];
	const struct fat_boot_sector_disk *boot_sec;
	int ret;
	u32 num_data_sectors;

	DEBUG("Reading FAT boot sector");

	if (full_pread(fd, buf, 512, 0) != 512)
		return -1;

	boot_sec = (struct fat_boot_sector_disk*)buf;
	memcpy(vol->oem_name, boot_sec->oem_name, sizeof(boot_sec->oem_name));
	remove_trailing_spaces(vol->oem_name);

	ret = fat_read_dos_2_0_bpb(vol, boot_sec);
	if (ret)
		return ret;

	ret = fat_read_dos_3_31_bpb(vol, boot_sec);
	if (ret)
		return ret;

	if (vol->type == FAT_TYPE_FAT32) {
		ret = fat_read_fat32_ebpb(vol, &boot_sec->ebpb.fat32.fat32_ebpb);
		if (ret)
			return ret;
		ret = fat_read_nonfat32_ebpb(vol, &boot_sec->ebpb.fat32.nonfat32_ebpb);
	} else {
		ret = fat_read_nonfat32_ebpb(vol, &boot_sec->ebpb.nonfat32_ebpb);
	}
	if (ret)
		return ret;

	num_data_sectors = vol->total_sectors - vol->reserved_sectors -
				((vol->max_root_entries << 5) >> vol->sector_order);
	vol->num_data_clusters = num_data_sectors >> vol->sectors_per_cluster_order;
	DEBUG("num_data_clusters = %u", vol->num_data_clusters);
	if (vol->num_data_clusters < 4085)
		vol->type = FAT_TYPE_FAT12;
	else if (vol->num_data_clusters < 65535)
		vol->type = FAT_TYPE_FAT16;
	else
		vol->type = FAT_TYPE_FAT32;
	return 0;
}

/* Map the first File Allocation Table into memory. */
static int
fat_map_fat(struct fat_volume *vol, int fd, int mount_flags)
{
	long page_size;
	void *ptr;
	size_t fat_size_bytes;
	size_t fat_aligned_size_bytes;
	off_t fat_offset;
	off_t fat_aligned_offset;
	int prot;

	page_size = sysconf(_SC_PAGESIZE);
	if (page_size <= 0 || !is_power_of_2(page_size) ||
	    page_size > (1U << 31))
	{
		/* Weird page size (shouldn't happen) */
		fat_error("Invalid system page size (%ld)", page_size);
		errno = EINVAL;
		return -1;
	}

	fat_offset = (off_t)vol->reserved_sectors << vol->sector_order;
	fat_aligned_offset = fat_offset & ~(page_size - 1);

	fat_size_bytes = (size_t)vol->sectors_per_fat << vol->sector_order;
	fat_aligned_size_bytes = fat_size_bytes + (fat_offset - fat_aligned_offset);

	if (fat_aligned_size_bytes < fat_size_bytes) {
		/* Unsigned integer overflow */
		fat_error("FAT size too large");
		errno = EINVAL;
		return -1;
	}

	fprintf(stderr,"Mapping FAT into memory (%zu bytes at offset %zu)\n",
	      fat_size_bytes, fat_offset);

	prot = PROT_READ;
	if (mount_flags & FAT_MOUNT_FLAG_READWRITE)
		prot |= PROT_WRITE;
	
	//mmap in the kernel TODO
	ptr = mmap(NULL, fat_aligned_size_bytes, prot, MAP_PRIVATE,
		   fd, fat_aligned_offset);
	
	if (ptr == MAP_FAILED)
		return -1;
	vol->fat_map = ptr + (fat_offset - fat_aligned_offset);
	uint32_t *p = vol->fat_map;
	int i, j; 
	int cluster_number = 0;
	uint32_t *empty_clusters = kmalloc(fat_aligned_size_bytes, GFP_KERNEL);
	uint8_t *cluster_contents = kmalloc(1, GFP_KERNEL);

	for (i = 0; i < 20; i++){
		fprintf(stderr, "cluster: %d status: %" PRIu32 "\n", i, p[i]);
		//for (j = 0; j<2; j++){
		//		pread(fd, cluster_contents, 1, 548864+(i*512)+j);
		//		fprintf(stderr, "contents: %" PRIu8, *cluster_contents);
		//		fprintf(stderr, "\n");
		//}
		if (p[i] == 0){
			empty_clusters[cluster_number] = i;
			//fprintf(stderr, "free cluster: %" PRIu32 "\n", empty_clusters[cluster_number]);
		 	cluster_number++;
		}
	}
	vol->empty_clusters = empty_clusters;
	
      	/*for (i = 0; i < 100; i++){
		fprintf(stderr, "cluster: %" PRIu32 "\n", vol->empty_clusters[i]);
	}*/
	fprintf(stderr, "clusters: %" PRIu32 "\n", vol->num_data_clusters);
	fprintf(stderr, "fat_map: %p \n", vol->fat_map);
	fprintf(stderr, "sector size %" PRIu16 "\n", vol->bytes_per_sector);
	fprintf(stderr, "sectors per cluster: %" PRIu8 "\n", vol->sectors_per_cluster);
	fprintf(stderr, "sector order: %" PRIu16 "\n", vol->sector_order);
	fprintf(stderr, "reserved sectors: %" PRIu16 "\n", vol->reserved_sectors);
	fprintf(stderr, "sectors per fat: %" PRIu32 "\n", vol->sectors_per_fat);
	return 0;
}

/* Open a FAT volume and prepare it for mounting by returning a
 * `struct fat_volume' containing information read from the
 * filesystem superblock.
 */
struct fat_volume *
fat_mount(const char *volume, int mount_flags)
{
	//initialize this stuff to null
	struct fat_volume *vol = NULL;
	int fd;
	int ret;
	int open_flags;

	DEBUG("Mounting FAT volume \"%s\"", volume);

	vol = kmalloc(1*sizeof(struct fat_volume), GFP_KERNEL);
	if (!vol)
		goto out;

	if (mount_flags & FAT_MOUNT_FLAG_READWRITE)
		open_flags = O_RDWR;
	else
		open_flags = O_RDONLY;

	fd = open(volume, open_flags);
	if (fd < 0)
		goto out_free_vol;

	/* Read the FAT boot sector */
	ret = fat_read_boot_sector(vol, fd);
	if (ret)
		goto out_close_fd;

	/* Map the first File Allocation Table into memory */
	ret = fat_map_fat(vol, fd, mount_flags);
	if (ret)
		goto out_close_fd;

	/* Initialize other fields of the `struct fat_volume' */
	vol->fd = fd;  /* File descriptor to use when reading data */
	vol->mount_flags = mount_flags;  /* Store the mount flags (not yet used
					    anywhere though) */
	vol->max_allocated_files = 100; /* Arbitrary soft limit, to keep memory
					   usage down. */
	//INIT_LIST_HEAD(&vol->lru_file_list); /* Initially, the LRU list of directories is empty. */

	/* Compute the offset of the first byte of the data area so it doesn't
	 * need to be re-calculated over and over. */
	vol->data_start_offset = (off_t)(vol->num_tables * vol->sectors_per_fat +
					 vol->reserved_sectors +
					 (vol->max_root_entries >> (vol->sector_order - 5)))
						<< vol->sector_order;
	fprintf(stderr, "data_start_offset = %"PRIu64"\n", vol->data_start_offset);

	/* Initialize the root directory */
	//fat_file_init(&vol->root, vol);
	//vol->root.dentry.attribs = FILE_ATTRIBUTE_DIRECTORY;
	//if (vol->type == FAT_TYPE_FAT32)
		//vol->root.dentry.start_cluster = vol->root_dir_start_cluster;
	goto out;
out_close_fd:
	close(fd);
out_free_vol:
	free(vol);
	vol = NULL;
out:
	return vol;
}

/* Unmount a FAT volume */
int
fat_unmount(struct fat_volume *vol)
{
	int ret;

	DEBUG("Unmounting FAT volume");

	ret = close(vol->fd);
	//mmap, figure this out, does this work in kernel
	munmap(vol->fat_map, (size_t)vol->sectors_per_fat << vol->sector_order);
	//fat_destroy_file_tree(&vol->root);
	kfree(vol);
	return ret;
}

/* Get the next cluster in the chain of clusters for a file or directory.
 * Returns FAT_CLUSTER_END_OF_CHAIN if this was the last cluster in the chain;
 * otherwise returns the number of the next cluster. */
 
u32
fat_next_cluster(struct fat_volume *vol, u32 cur_cluster)
{
	/* The FAT entries are stored differently depending on the FAT type. */

	u32 next_cluster;
	switch (vol->type) {
	case FAT_TYPE_FAT12: {
			const u8 *p = (const u8*)vol->fat_map;
			size_t byte_offset = (off_t)cur_cluster * 3 / 2;
			if (byte_offset % 3 == 0) {
				next_cluster = p[byte_offset] |
						((u32)p[byte_offset + 1] & 0xf) << 8;
			} else {
				next_cluster = (p[byte_offset] >> 4) |
						((u32)p[byte_offset + 1] << 4);
			}
		}
		break;
	case FAT_TYPE_FAT16:
		next_cluster = le16_to_cpu(((const le16*)vol->fat_map)[cur_cluster]);
		break;
	case FAT_TYPE_FAT32:
		next_cluster = le32_to_cpu(((const le32*)vol->fat_map)[cur_cluster]);
		break;
	default:
		/* Shouldn't be reached */
		next_cluster = FAT_CLUSTER_END_OF_CHAIN;
		break;
	}
	DEBUG("next_cluster = %u", next_cluster);

	/* We currently don't check for the actual special cluster values, but
	 * instead treat all out of range values as end-of-chain.  This may be
	 * okay for read-only mounts. */
	if (!fat_is_valid_cluster_number(vol, next_cluster))
		next_cluster = FAT_CLUSTER_END_OF_CHAIN;
	return next_cluster;
}
