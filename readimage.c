#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "ext2.h"

unsigned char *disk;

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

int check_inode(int index, unsigned char *imap) {
	return (imap[index/8] >> (index % 8)) & 0x1;
}

int main(int argc, char **argv) {

    if(argc != 2) {
        fprintf(stderr, "Usage: %s <image file name>\n", argv[0]);
        exit(1);
    }
    int fd = open(argv[1], O_RDWR);

    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    struct ext2_super_block *sb = (struct ext2_super_block *)(disk + 1024);
	struct ext2_group_desc *desc = (struct ext2_group_desc *)(disk + 2*1024); 
    printf("Inodes: %d\n", sb->s_inodes_count);
    printf("Blocks: %d\n", sb->s_blocks_count);
	printf("Block group:\n");
	printf("    block bitmap: %d\n", desc->bg_block_bitmap);
	printf("    inode bitmap: %d\n", desc->bg_inode_bitmap);
	printf("    inode table: %d\n", desc->bg_inode_table);
	printf("    free blocks: %d\n", sb->s_free_blocks_count); //should it be desc->free blocks count??????
	printf("    free inodes: %d\n", sb->s_free_inodes_count);
	printf("    used_dirs: %d\n", desc->bg_used_dirs_count);
  
	// Printing block bitmap byte by byte 
    printf("Block bitmap: ");
	unsigned char *map = (unsigned char*)(disk + EXT2_BLOCK_SIZE * desc->bg_block_bitmap);
	int i;
	for (i = 0; i < sb->s_blocks_count; i++) {
		if (i != 0 && i % 8 == 0) {
			printf(" ");
		}
		printf("%d", (map[i/8] >> (i % 8)) & 0x1);
	}
	printf("\n");

	// Printing inode bitmap byte by byte
	printf("Inode bitmap: ");
	unsigned char *imap = (unsigned char *)(disk + 1024 * desc->bg_inode_bitmap);
	for (i = 0; i < sb->s_inodes_count; i++) {
		if (i != 0 && i%8 == 0) {
			printf(" ");
		}
		printf("%d", (imap[i/8] >> (i%8)) & 0x1);
	}
	printf("\n");

	// Printing important inodes (2, >11)
	printf("\nInodes:\n");
	struct ext2_inode *curinode;
	unsigned char inode_type;
	int node;
	for (node = 0; node < 32; node++) {
		//printf("%d", check_inode(node, imap));
		if (check_inode(node, imap) && (node == 1 || node >= 11)) {
			curinode = (struct ext2_inode *)((disk + 1024 * desc->bg_inode_table) + (sb->s_inode_size * (node)));
			inode_type = get_inode_type(curinode);
			printf("[%d] type: %c size: %d links: %d blocks: %d\n[%d] Blocks:",
				node+1, inode_type, curinode->i_size, curinode->i_links_count, curinode->i_blocks, node+1);
			for (i = 0; curinode->i_block[i] != 0; i++) {
				printf(" %d", curinode->i_block[i]);
			}
			printf("\n");
		}
	}

	// Printing directory blocks
	printf("\nDirectory Blocks:\n"); 
	// Loop through valid inodes again to find directory blocks
	for (node = 0; node < 32; node++) {
		if (check_inode(node, imap) && (node == 1 || node >= 11)) {
			curinode = (struct ext2_inode *)((disk + 1024 * desc->bg_inode_table) + (sb->s_inode_size * node));
			if (get_inode_type(curinode) == 'd') {
				// Printing the DIR BLOCK NUM line.
				printf("   DIR BLOCK NUM: ");
				for (i = 0; curinode->i_block[i] != 0; i++) {
					printf("%d ", curinode->i_block[i]);				
				}
				printf("(for inode %d)\n", node+1);

				// Looping through the data blocks to find and print the ext2_dir_entry details
				unsigned int cur_rec_len;
				unsigned char file_type;
				char filename[256];
				struct ext2_dir_entry *cur_dir;
				for (i = 0; curinode->i_block[i] != 0; i++) {
					// Currently results in some strange infinite loop, reading wrong block????
					for (cur_rec_len = 0; cur_rec_len < 1024; ) {
						// Reset filename to null-terminators
						memset(filename, '\0', sizeof(filename));
						// Grabbing the directory, setting necessary variables for readability
						cur_dir = (struct ext2_dir_entry *)(disk + (1024 * curinode->i_block[i]) + cur_rec_len);
						strncpy(filename, cur_dir->name, cur_dir->name_len);
						file_type = get_dir_type(cur_dir->file_type);
						// Increasing cur_rec_len for next directory
						cur_rec_len += cur_dir->rec_len;
						// Doing the actual print
						printf("Inode: %d rec_len: %d name_len: %d type= %c name=%s\n",
								cur_dir->inode, cur_dir->rec_len, cur_dir->name_len, file_type, filename);
					}
				}
			}
		}
	}
    return 0;
}


