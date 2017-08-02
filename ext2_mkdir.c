/*
 * Takes two arguments:
 * First: name of an ext2 formatted disk.
 * Second: absolute path on the ext2 formatted disk.
 *
 * This program should work like mkdir, creating the final directory on the
 * specified path on the disk.
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
#include "ext2.h"
#include<errno.h>
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
	// Getting the parent inode index
	int parent_inode_index = check_parent(disk, argv[2]);
	if (parent_inode_index == -1) { // Directory not found.
		fprintf(stderr, "Directory does not exist.");
		exit(ENOENT);
	}
	// Get new dir name
	int i;
	int dir_len = 0;
	if (argv[2][strlen(argv[2]) -1] == '/') argv[2][strlen(argv[2])-1] = '\0';
	for (i = strlen(argv[2]); argv[2][i] != '/'; i--) {
		dir_len++;
		if (dir_len > 255) {
			fprintf(stderr, "Directory name too large.");
			exit(1);
		}
	}
	char new_dir[dir_len];
	strcpy(new_dir, argv[2] + i + 1);
	// Check that there aren't any files with that name.
	struct ext2_inode *parent = (struct ext2_inode *)((disk + 1024 * desc->bg_inode_table) +
		   											(sb->s_inode_size * parent_inode_index));
	if (search_directories(disk, parent, new_dir, 0) != -1) {
		fprintf(stderr, "Directory name already in use.\n");
		exit(EEXIST);
	}
	// Finding a free inode and allocating (set to 1)
	unsigned char *imap = (unsigned char *)(disk + 1024 * desc->bg_inode_bitmap);
	int inode;
	for (inode = 11; inode < sb->s_inodes_count; inode++) {
		if (check_node(inode, imap) == 0) {
			break;
		}
	}
	if (inode == sb->s_inodes_count) { // Maximum reached, no more inodes
		fprintf(stderr, "No more inodes available.");
		exit(1);
	}
	// Finding a free block and allocating (set to 1)
	unsigned char *bmap = (unsigned char *)(disk + 1024 * desc->bg_block_bitmap);
	int bnode;
	for (bnode = 22; bnode < sb->s_blocks_count; bnode++) {
		if (check_node(bnode, bmap) == 0) {
			break;
		}
	}
	if (bnode == sb->s_blocks_count) {
		fprintf(stderr, "No more blocks available.");
		exit(1);
	}
	// Actual allocation.
	set_node(inode, imap);
	set_node(bnode, bmap);
	adjust_free_blocks(-1, sb, desc);
	adjust_free_inodes(-1, sb, desc);
	// Writing data to the ext2_inode struct in the inode index.
	struct ext2_inode *new_inode = (struct ext2_inode *)((disk + 1024 * desc->bg_inode_table)
														 + (sb->s_inode_size * (inode)));
	new_inode->i_mode = EXT2_S_IFDIR;
	new_inode->i_uid = 0;
	new_inode->i_size = 1024;
	new_inode->i_ctime = (unsigned int) time(0);
	new_inode->i_dtime = 0;
	new_inode->i_gid = 0;
	new_inode->i_blocks = 2;
	new_inode->osd1 = 0;
	new_inode->i_block[0] = bnode;
	new_inode->i_generation = 0;
	new_inode->i_file_acl = 0;
	new_inode->i_dir_acl = 0;
	new_inode->i_faddr = 0;
	new_inode->i_links_count = 2; // Itself and from parent.
	parent->i_links_count++; // Parent gets one more link from parent dir in new directory.
	// Adding self and parent directories to allocated block.
	struct ext2_dir_entry *self = (struct ext2_dir_entry *)(disk + (1024 * bnode));
	self->inode = inode + 1; // the variable inode is the index, not actual inode.
	self->rec_len = 12; // self rec-len is always 12
	self->name_len = 1; // length of . is 1
	self->file_type = EXT2_FT_DIR;
	strncpy(self->name, ".", 1);
	struct ext2_dir_entry *par = (struct ext2_dir_entry *)(disk + (1024 * bnode) + self->rec_len);
	par->inode = parent_inode_index + 1;
	par->rec_len = 1024 - 12; // Fill in the rest of the 1024 bytes of space
	par->name_len = 2; // length of .. is 2
	par->file_type = EXT2_FT_DIR;
	strncpy(par->name, "..", 2);
	// Adding new inode/directory entry into the parent block.
	int par_block_index = 0;
	for (i = 0; parent->i_block[i] != 0; i++) {
		par_block_index = parent->i_block[i];
	}
	int total_rec_len;
	struct ext2_dir_entry *curdir = (struct ext2_dir_entry *)(disk + (1024 * par_block_index));
	// When this for loop exits, curdir should be the last directory 
	for (total_rec_len = 0; total_rec_len + curdir->rec_len < 1024; ) {
		total_rec_len += curdir->rec_len;
		curdir = (struct ext2_dir_entry *)(disk + (1024 * par_block_index) + total_rec_len);
	}
	// Check there's enough space to place the new dir entry in the current block
	// Reuse total_rec_len to find remaining bytes if curdir was reduced to proper size
	struct ext2_dir_entry *newdir;
	int remaining_rec_len = sizeof(curdir) + curdir->name_len; // Actual size of curdir
	remaining_rec_len += 4 - (remaining_rec_len % 4); // Add padding
	remaining_rec_len = 1024 - remaining_rec_len - total_rec_len; // Remaining space
	if (remaining_rec_len < (8 + strlen(new_dir))) { // Not enough space
		// Allocate new block
		int newblock;
		for (newblock = 22; newblock < sb->s_blocks_count; newblock++) {
			if (check_node(newblock, bmap) == 0) {
				break;
			}
		}
		if (newblock == sb->s_blocks_count) {
			fprintf(stderr, "No more blocks available.");
			exit(1);
		}
		set_node(newblock, bmap);
		adjust_free_blocks(-1, sb, desc);
		parent->i_block[i] = newblock; // i should still hold the first block space unused by parent
		parent->i_blocks += 2; // One more block being added
		newdir = (struct ext2_dir_entry *)(disk + (1024 * newblock));
		newdir->rec_len = 1024; // Takes up the whole of the new block.
	} else { // Enough space
		// Change curdir, add new directory
		curdir->rec_len = sizeof(curdir) + curdir->name_len;
		curdir->rec_len += 4 - (curdir->rec_len % 4);
		total_rec_len += curdir->rec_len;
		newdir = (struct ext2_dir_entry *)(disk + (1024 * par_block_index) + total_rec_len);
		newdir->rec_len = 1024 - total_rec_len; // Takes up the rest of the block.
	}
	newdir->inode = inode + 1;
	newdir->name_len = strlen(new_dir);
	newdir->file_type = EXT2_FT_DIR;
	strncpy(newdir->name, new_dir, newdir->name_len);

	// Done with the directories, minor upkeep
	desc->bg_used_dirs_count++;
	return 0;
}
