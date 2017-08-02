/*
 * Takes only one argument:
 * First: the name of an ext2 formatted disk.
 *
 * The program should implement a lightweight file system checker, which
 * detects a small subset of possible file system inconsistencies and takes
 * actions to fix them.
 */

#include<stdio.h>
#include<string.h>
#include<unistd.h>
#include<stdlib.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<sys/mman.h>
#include "ext2.h"
#include<errno.h>
#include "ext2_utils.c"

unsigned char *disk;
struct ext2_super_block *sb;
struct ext2_group_desc *desc;

/* MAIN */
int main(int argc, char **argv) {
	if (argc != 2) { // Requires only one argument, an ext2 formatted disk.
		fprintf(stderr, "Usage: %s [disk]\n", argv[0]);
		exit(1);
	}
	// Opening disk
	int fd = open(argv[1], O_RDWR);
	if (!fd) {
		fprintf(stderr, "Disk image '%s' not found.", argv[1]);
		exit(ENOENT);
	}
	// mmap the disk
	disk = mmap(NULL, 128*1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (disk == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}
	// Grabbing super block and block descriptor
	sb = (struct ext2_super_block *)(disk + 1024);
	desc = (struct ext2_group_desc *)(disk + 2*1024);

	int errors = 0; // Total number of errors fixed, increment for every fix.
	// Allocating the bitmaps.
	unsigned char *map = (unsigned char *)(disk + 1024 * desc->bg_block_bitmap);
	unsigned char *imap = (unsigned char *)(disk + 1024 * desc->bg_inode_bitmap);

	// Count the free blocks based on the bitmap.
	int free = sb->s_blocks_count; // Total number of blocks.
	int diff; // Value to allocate the difference if there is one.
	int i;
	for (i = 0; i < sb->s_blocks_count; i++) {
		if (check_node(i, map)) free--;
	}
	// Check free versus the counts in sb and desc.
	// Block bitmap vs superblock
	if (free != sb->s_free_blocks_count) {
		diff = abs(sb->s_free_blocks_count - free);
		printf("Fixed: superblock's free block counter was off by %d compared to the bitmap\n",
					diff);
		sb->s_free_blocks_count = free;
		errors += diff;
	}
	// Block bitmap vs group descriptor
	if (free != desc->bg_free_blocks_count) {
		diff = abs(desc->bg_free_blocks_count - free);
		printf("Fixed: block group's free block counter was off by %d compared to the bitmap\n",
					diff);
		desc->bg_free_blocks_count = free;
		errors += diff;
	}
	// Reset free for the inode bitmap, loop through.
	free = sb->s_inodes_count;
	for (i = 0; i < sb->s_inodes_count; i++) {
		if (check_node(i, imap)) free--;
	}
	// Check free versus the counts in sb and desc.
	// Inode bitmap vs superblock
	if (free != sb->s_free_inodes_count) {
		diff = abs(sb->s_free_inodes_count - free);
		printf("Fixed: superblock's free inode counter was off by %d compared to the bitmap\n",
					diff);
		sb->s_free_inodes_count = free;
		errors += diff;
	}
	// Inode bitmap vs group desc
	if (free != desc->bg_free_inodes_count) {
		diff = abs(desc->bg_free_inodes_count - free);
		printf("Fixed: block group's free inode counter was off by %d compared to the bitmap\n",
					diff);
		desc->bg_free_inodes_count = free;
		errors += diff;
	}

	// Check each directory entry for matching file type with its inode.
	// Straight from readimage to get directory blocks.
	struct ext2_inode *curinode;
	struct ext2_inode *file_node;
	for (i = 0; i < sb->s_inodes_count; i++) {
		if ((check_node(i, imap) && (i == 1 || i >= 11)) || i == 1) {
			curinode = (struct ext2_inode *)((disk + 1024 * desc->bg_inode_table) + (sb->s_inode_size * i));
			if (get_inode_type(curinode) == 'd') {
				// Looping through blocks to find ext2_dir_entry details
				unsigned int cur_rec_len;
				unsigned char file_type;
				struct ext2_dir_entry *curdir;
				int x; // Need some new loop integer since i is already in use.
				for (x = 0; curinode->i_block[x] != 0; x++) {
					for (cur_rec_len = 0; cur_rec_len < 1024; ) {
						curdir = (struct ext2_dir_entry *)(disk + (1024 * curinode->i_block[x]) + cur_rec_len);
						file_type = get_dir_type(curdir->file_type);
						file_node = (struct ext2_inode *)((disk + 1024 * desc->bg_inode_table) +
															(sb->s_inode_size * (curdir->inode-1)));
						cur_rec_len += curdir->rec_len;
						if (file_type != get_inode_type(file_node)) {
							printf("Fixed: Entry type vs inode mismatch: inode %d\n", curdir->inode);
							errors++;
							set_dir_type(curdir, get_inode_type(file_node));
						}

						// Check that the directory's inode is allocated
						if (!check_node(curdir->inode - 1, imap)) { // Inode is not allocated
							errors++;
							printf("Fixed: inode %d not marked as in-use\n", curdir->inode);
							set_node(curdir->inode-1, imap);
							adjust_free_inodes(-1, sb, desc);
						}

						// Check inodes i_dtime to be 0
						if (file_node->i_dtime != 0) {
							errors++;
							printf("Fixed: valid inode marked for deletion: %d\n", curdir->inode);
							file_node->i_dtime = 0;
						}

						// Check the directory's data blocks for allocation
						int y; // Another random counter...
						int block_counter = 0; // Counting amount of blocks not allocated.
						for (y = 0; file_node->i_block[y] != 0; y++) {
							if (!check_node(file_node->i_block[y] - 1, map)) {
								block_counter++;
								set_node(file_node->i_block[y] - 1, map);
								adjust_free_blocks(-1, sb, desc);
							}
						}
						if (block_counter > 0) {
							errors++;
							printf("Fixed: %d in-use data blocks not marked in data bitmap for inode: %d\n",
									block_counter, curdir->inode);
						}	
					}
				}
			}
		}
	}

	// Output final message
	if (errors > 0) {
		printf("%d file system inconsistencies repaired!\n", errors);
	} else {
		printf("No file system inconsistencies detected!\n");
	}
		
	return 0;
}
