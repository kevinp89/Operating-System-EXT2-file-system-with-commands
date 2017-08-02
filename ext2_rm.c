/*
 * Takes two arguments:
 * First: the name of an ext2 formatted disk.
 * Second: absolute path to a file or link (not directory) on the disk.
 *
 * This program should behave like rm, removing the specified file from the
 * disk.
 */

#include<stdio.h>
#include<string.h>
#include<unistd.h>
#include<stdlib.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<sys/mman.h>
#include<time.h>
#include<errno.h>
#include "ext2.h"
#include "ext2_utils.c"

unsigned char *disk;
struct ext2_super_block *sb;
struct ext2_group_desc *desc;

/* MAIN */

int main(int argc, char **argv) {
	// We need two arguments, so argc must be 3.
	if (argc != 3) {
		fprintf(stderr, "Usage: %s [disk] [path]\n", argv[0]);
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
	// Grabbing inode map and data map
	unsigned char *imap = (unsigned char *)(disk + 1024 * desc->bg_inode_bitmap);
	unsigned char *map = (unsigned char *)(disk + 1024 * desc->bg_block_bitmap);
	// Get parent inode index
	int parent_inode_index = check_parent(disk, argv[2]);
	if (parent_inode_index == -1) { // Directory not found.
		fprintf(stderr, "Directory does not exist.");
		exit(ENOENT);
	}
	// Get file name
	int i;
	int filename_len = 0;
	if (argv[2][strlen(argv[2])-1] == '/') {
		fprintf(stderr, "Invalid filename.");
		exit(ENOENT);
	}
	for (i = strlen(argv[2]); argv[2][i] != '/'; i--) {
		filename_len++;
	}
	char filename[filename_len];
	strcpy(filename, argv[2] + i + 1);
	// Check that the file exists
	struct ext2_inode *parent = (struct ext2_inode *)((disk + 1024 * desc->bg_inode_table) +
													(sb->s_inode_size * parent_inode_index));
	int targ_inode = search_directories(disk, parent, filename, 0);
	if (targ_inode == -1) {
		fprintf(stderr, "File does not exist.");
		exit(ENOENT);
	}
	// Find whether the directory is a link or file
	struct ext2_inode *target = (struct ext2_inode *)((disk + 1024 * desc->bg_inode_table) +
													(sb->s_inode_size * targ_inode));
	if (get_inode_type(target) == 'd') {
		fprintf(stderr, "Cannot remove directories.");
		exit(ENOENT);
	} else {
		// Directory stuff straight from readimage.c
		int cur_rec_len;
		char curfilename[256];
		struct ext2_dir_entry *cur_dir;
		struct ext2_dir_entry *prev_dir;
		for (i = 0; parent->i_block[i] != 0; i++) {
			prev_dir = NULL;
			for (cur_rec_len = 0; cur_rec_len < 1024; ) {
				memset(curfilename, 0, sizeof(curfilename));
				cur_dir = (struct ext2_dir_entry *)(disk + (1024 * parent->i_block[i]) + cur_rec_len);
				strncpy(curfilename, cur_dir->name, cur_dir->name_len);
				cur_rec_len += cur_dir->rec_len;
				// Checking if the current file is the file we're looking for
				if (strcmp(curfilename, filename) == 0) {
					if (prev_dir != NULL) {
						prev_dir->rec_len += cur_dir->rec_len;
						break;
					} else {
						// rec-len is 1024, dir_entry has its own data block
						parent->i_block[i] = 0;
						set_node(parent->i_block[i] - 1, map);
						adjust_free_blocks(1, sb, desc);
					}
				} else {
					// Setting the prev_dir to modify later
					prev_dir = cur_dir;
				}
			}
		}
		target->i_links_count--;
		if (target->i_links_count <= 0) {
			// De-allocate everything, set inode's i_dtime, find directory and set prev dir's rec_len over
			target->i_dtime = (unsigned int)time(0);
			set_node(targ_inode, imap);
			adjust_free_inodes(1, sb, desc);
			// De-allocate the data blocks as well
			for (i = 0; target->i_block[i] != 0; i++) {
				set_node(target->i_block[i] - 1, map);
				adjust_free_blocks(1, sb, desc);
			}
		}
	}

	return 0;
}
