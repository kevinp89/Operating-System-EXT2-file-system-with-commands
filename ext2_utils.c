/*
 * File containing all the helper functions required among the various
 * ext2_* programs.
 *
 * Remember to keep helpers as general as possible so special cases are 
 * less likely.
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

/* HELPERS */

/* Returns the node value at the index of the bitmap map. */
int check_node(int index, unsigned char *map) {
	return (map[index/8] >> (index % 8)) & 0x1;
}

/* Sets the node value at the index in the bitmap map. */
void set_node(int index, unsigned char *map) {
	map[index/8] ^= (1 << (index % 8));
}

/* Gets the type of the input file_type, for directories. */
unsigned char get_dir_type(unsigned char file_type) {
	if (file_type == EXT2_FT_REG_FILE) {
		return 'f';
	} else if (file_type == EXT2_FT_DIR) {
		return 'd';
	} else if (file_type == EXT2_FT_SYMLINK) {
		return 'l';
	} else {
		return '?';
	}
}

/* Gets the type of the input inode in character form. */
unsigned char get_inode_type(struct ext2_inode *ip) {
	if (S_ISREG(ip->i_mode)) {
		return 'f';
	} else if (S_ISDIR(ip->i_mode)) {
		return 'd';
	} else if (S_ISLNK(ip->i_mode)) {
		return 'l';
	} else {
		return '?';
	}
}

/* Sets the file_type for the input dir_entry, based on the char input */
void set_dir_type(struct ext2_dir_entry *dir_entry, unsigned char type) {
	switch(type) {
		case 'f':
			dir_entry->file_type = EXT2_FT_REG_FILE;
			break;
		case 'd':
			dir_entry->file_type = EXT2_FT_DIR;
			break;
		case 'l':
			dir_entry->file_type = EXT2_FT_SYMLINK;
			break;
		case '?':
		default:
			dir_entry->file_type = EXT2_FT_UNKNOWN;
	}
}

/* Searches the directories in an inode for a target directory 
 * Returns the dir_entry's inode if it exists, -1 otherwise.
 */
unsigned int search_directories(unsigned char *disk, struct ext2_inode *node, char *target, int dir_only) {
	// Dir related
	unsigned int cur_rec_len;
	char filename[256];
	struct ext2_dir_entry *cur_dir;

	int i;
	for (i = 0; node->i_block[i] != 0; i++) {
		for (cur_rec_len = 0; cur_rec_len < 1024; ) {
			cur_dir = (struct ext2_dir_entry *)(disk + (1024 * node->i_block[i]) + cur_rec_len);
			cur_rec_len += cur_dir->rec_len;
			memset(filename, '\0', sizeof(filename));
			strncpy(filename, cur_dir->name, cur_dir->name_len);
			if (strcmp(filename, target) == 0) { // Found matching dir
				if (dir_only) { // If we're only checking for directories
					if (get_dir_type(cur_dir->file_type) == 'd') {
						return cur_dir->inode-1; // Return index, not the node itself.
					}
				} else { // Check all types of files
					return cur_dir->inode-1;
				}
			}
		}
	}
	return -1;
}

/* Checks if the parent path exists. 
 * Returns the parent inode index if it does and -1 if it doesn't.
 */
int check_parent(unsigned char *disk, char *path) {
	struct ext2_super_block *sb = (struct ext2_super_block *)(disk + 1024);
	struct ext2_group_desc *desc = (struct ext2_group_desc *)(disk + 2*1024);
	// Copying the path string into an array
	// Getting amount of dir inputs
	char *dupe = strdup(path);
	strcpy(dupe, path);
	int dircount = 0;
	int i;
	for (i = 0; dupe[i] != 0; i++) {
		if (dupe[i] == '/' && dupe[i+1] != '\0') {
			dircount++;
		}
	}
	if (dircount == 0) {
		fprintf(stderr, "Invalid directory.\n");
		exit(ENOENT);
	}
	// Creating array and copying
	char *dirs[dircount];
	char *tail = dupe;
	for (i = 0; i < dircount; i++) {
		dirs[i] = strsep(&tail, "/");
	}

	// Inode related
	struct ext2_inode *curinode;
	int node = 1; // Root directory begins at index 1
	
	i = 1;
	while (i < dircount) {
		if (strcmp(dirs[i], ".") != 0) {
			curinode = (struct ext2_inode *)((disk + 1024 * desc->bg_inode_table) + (sb->s_inode_size * node));
			node = search_directories(disk, curinode, dirs[i], 1);
			if (node == -1) {
				break;
			}
		}
		i++;
	}

	free(dupe);
	return node;
}

/* Increments or decrements the free block count by n in superblock sb and group descriptor desc */
void adjust_free_blocks(int n, struct ext2_super_block *sb, struct ext2_group_desc *desc) {
	sb->s_free_blocks_count += n;
	desc->bg_free_blocks_count += n;
}

/* Increments or decrements the free inode count by n in superblock sb and group descriptor desc */
void adjust_free_inodes(int n, struct ext2_super_block *sb, struct ext2_group_desc *desc) {
	sb->s_free_inodes_count += n;
	desc->bg_free_inodes_count += n;
}
