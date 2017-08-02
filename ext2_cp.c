/*
 * Takes three command line arguments:
 * First: the name of ext2 formatted disk.
 * Second: the path to a file on your native operating system.
 * Third: absolute path on your ext2 formatted disk.
 *
 * The program should work like cp, copying the file on your native system
 * to the specified location on the disk.
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
#include<time.h>

unsigned char *disk;
struct ext2_super_block *sb;
struct ext2_group_desc *desc;


/* HELPERS */

int check_path(char *path){
	if(path[0] != '/'){
		return 1;	
	} else {
		return 0;
	}

}

//get free blocks from the blocks bitmap
int *get_free_blocks(int blocks_need, unsigned char *block_bit_map){

	int i,j=22;
	
	int* free_blocks = malloc(sizeof(int)*blocks_need);
	
	for(i=0; i<blocks_need; i++){
		while(check_node(j, block_bit_map) == 0){
			j++;
			//if j reaches max block count, return null
			if(j==sb->s_blocks_count){
				return NULL;
			}
		}
		free_blocks[i] = j;	
		set_node(j, block_bit_map);
	}
	return free_blocks;
}

//find the index of a free block in the bitmap
int find_free_block(unsigned char *bmap){
	int bnode;
	for (bnode = 22; bnode < sb->s_blocks_count; bnode++) {
		if (check_node(bnode, bmap) == 0) {
			break;
		}
	}
	if(bnode == sb->s_blocks_count){
		return -1;	
	}
	set_node(bnode, bmap);
	return bnode;
}

/**
	TODO: return instead of exit
	generalize some piece of code into helpers

*/
int main(int argc, char** argv){
	//arguments check
	if(argc != 4){
		fprintf(stderr, "Usage: %s [disk] [os path] [virtual disk path]\n", argv[0]);
		exit(1);
	}
	//opening the disk
	int fd = open(argv[1], O_RDWR);
	if(!fd){
		fprintf(stderr, "Disk image '%s' not found\n", argv[1]);
		exit(1);	
	}
    //check virtual path is absolute or not.
    int check = check_path(argv[3]);
    if(check == 1){
        fprintf(stderr, "Please provide absolute path for virtual path");
        exit(1);
    }
	//open the file from this os
	int osfd = open(argv[2], O_RDONLY);
	if(osfd == -1){
		fprintf(stderr, "os path: '%s' invalid\n", argv[2]);
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
	
	//get the file size
	int file_size = lseek(osfd, 0, SEEK_END);

	//calculate number of blocks needed to store file
	int blocks_needed = (file_size - 1) / EXT2_BLOCK_SIZE + 1;
	
	//check if there is space in disk img
	if(blocks_needed > sb->s_free_blocks_count){
		fprintf(stderr, "Not enough space in disk img\n");
		exit(1);
	}
	
	
	//get the bitmaps and inode table
	//int inode_bitmap = desc-> bg_inode_bitmap;
	unsigned char *block_bitmap = (unsigned char *) (disk+1024 * desc -> bg_block_bitmap);
	//int inode_table =  desc -> bg_inode_table;

	//copy osfilename into a variable
	char *dup = strdup(argv[2]);
	// Creating array and copying
	int c;
	int count=0;
	for (c = 0; dup[c] != 0; c++) {
		if (dup[c] == '/' && dup[c+1] != '\0') {
			count++;
		}
	}
	if(count!=0) {

		char *dirs[count];
		int cp;
		for (cp = 0; cp < count; cp++) {
			dirs[cp] = strsep(&dup, "/");
		}

		dup = dirs[cp - 1];
	}

	//we need the parent directory where file should belong
	int parent_index;
	//we need a temp variable to hold the virtual disk path
	char *virtual_path = strdup(argv[3]);
	//if it ends in / then append in the os file name
	if(argv[3][strlen(argv[3]) - 1] == '/'){
		//copy path into temp variable
		strcat(virtual_path, dup);
        //get the parent index
		parent_index = check_parent(disk, virtual_path);
		if(parent_index == -1){
			fprintf(stderr, "'%s' No such file or directory\n", argv[3]);
			exit(1);
		}
	} else {
        //doesnt end in / so check the parent (it could be a dir could be a new name
		parent_index = check_parent(disk, argv[3]);

		if(parent_index == -1){
			fprintf(stderr, "'%s' No such file or directory\n", argv[3]);
			exit(1);
		}
	}

	//get the parent node
	struct ext2_inode *parent_node = (struct ext2_inode *) ((disk+EXT2_BLOCK_SIZE * desc->bg_inode_table) +
            (sb->s_inode_size * parent_index));

	//take out the last / and get the file name (could be a dir)
	if (virtual_path[strlen(virtual_path) -1] == '/') virtual_path[strlen(virtual_path)-1] = '\0';
	
	/**get the new file name */
    //get the length of the name
	int i;
	int file_len1 = 0;
	for (i = strlen(virtual_path); virtual_path[i] != '/'; i--) {
		file_len1++;
		if (file_len1 > 255) {
			fprintf(stderr, "File name too large.\n");
			exit(1);
		}
	}
	
	//copy the new file name
	char file_name[file_len1];
	strcpy(file_name, virtual_path + i + 1);

	//check if last name is a directory
	int new_parent_index;
	new_parent_index = search_directories(disk, parent_node, file_name, 1);

	//if its a directory, thats the new parent, otherwise file_name is our new file name
	if(new_parent_index != -1){
		parent_node = (struct ext2_inode *) ((disk+EXT2_BLOCK_SIZE * desc->bg_inode_table) + (sb->s_inode_size *
				new_parent_index));
	}

	//if last name is a file and already exists, throw an err
	if(search_directories(disk, parent_node, file_name, 0) != -1){
		fprintf(stderr, "File name '%s' already exists", file_name);
		exit(1);	
	}


	//last name is either our new parent or a new file. all is set
	
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
	
	//new inode init
	struct ext2_inode *new_inode = (struct ext2_inode *)((disk + 1024 * desc->bg_inode_table)
														 + (sb->s_inode_size * (inode)));
	
	//set up the inode
	new_inode->i_mode = EXT2_S_IFREG; //file flag
	new_inode->i_size = file_size;
    new_inode->i_ctime = (unsigned int) time(0);
    new_inode->i_dtime = 0;
	new_inode->i_blocks = 1;
	new_inode->i_links_count = 1;
	sb->s_free_inodes_count--;
	desc->bg_free_inodes_count--;

	//map the src file
	unsigned char *source = mmap(NULL, file_size, PROT_READ| PROT_WRITE,
								 MAP_PRIVATE, osfd, 0);

	void *db; //block where data of the file belongs.
    void *indirect_block_ptr;
	unsigned int indir_data_blocks[15];
	//need to find free blocks for the file and read into them
	int block_indx;
    int copied = 0; //tracker for how much copied
    int size_remain = file_size; // tracker for how much remains
	for(block_indx=0; block_indx<blocks_needed; block_indx++){
		//if indirect blocks aren't needed
		if(block_indx<12){
			//find a free block
			int free_block = find_free_block(block_bitmap);
			if (free_block == -1) {
				fprintf(stderr, "No more blocks available.");
				exit(1);
			}
			//update superblock and new_inode
			adjust_free_blocks(-1, sb, desc);
			new_inode->i_blocks++;
			new_inode->i_block[block_indx] = (sb->s_first_data_block) + free_block-1;
			//update data block
			db = (void*)(disk + EXT2_BLOCK_SIZE * new_inode->i_block[block_indx]);
		}
		
		//if indirect blocks are needed
		//need to setup an indirect block
		else if(block_indx>=12){
			int free_block = find_free_block(block_bitmap);
			if(free_block == -1){
				fprintf(stderr, "No more blocks available.");
				exit(1);
			}
			
			//initialize an indirect block if i==12, since i_blocks[12] is the pointer to the indirect block
			if(i==12){
				//find space for this indirect block to set up a pointer to blocks
 				int indir_block_indx = find_free_block(block_bitmap);
				if(indir_block_indx == -1){
					fprintf(stderr, "No more blocks available.");
					exit(1);
				}
				new_inode->i_block[block_indx] = (sb->s_first_data_block) + indir_block_indx;
				indirect_block_ptr = (void*)(disk + EXT2_BLOCK_SIZE * new_inode->i_block[block_indx]);	
				indir_data_blocks[block_indx-12]  = (sb->s_first_data_block) + free_block - 1;
				memcpy(indirect_block_ptr, (void*)indirect_block_ptr, 16);
				
			}
			
			//update the new_node
			new_inode->i_block[block_indx] = (sb->s_first_data_block) + free_block;
			//update superblock
			adjust_free_blocks(-1, sb, desc);
			//update the data block
			db = (void*)(disk + EXT2_BLOCK_SIZE * indir_data_blocks[block_indx-12]);
		}
        if(size_remain < EXT2_BLOCK_SIZE) {
            memcpy(db, source + copied, size_remain);
            size_remain = 0;
            break;
        } else {
            memcpy(db, source + copied, EXT2_BLOCK_SIZE);
            size_remain -= EXT2_BLOCK_SIZE;
            copied += EXT2_BLOCK_SIZE;
        }

	}
	// set the new inode in the imap
	set_node(inode, imap);
	// set the block sectors occupied
	new_inode->i_blocks = (unsigned int)((new_inode->i_size / 512) + 1);
	/* update parent directory */
	
	int par_block_index;
	int k;
	for(k=0; parent_node->i_block[k] != 0; k++){
		par_block_index = parent_node -> i_block[k];	
	}

	struct ext2_dir_entry *curdir = (struct ext2_dir_entry *)(disk + (1024 * par_block_index));
	int total_rec_len;
	//make curdir the last dir
	for(total_rec_len=0; total_rec_len + curdir->rec_len < 1024;){
		total_rec_len += curdir->rec_len;
		curdir = (struct ext2_dir_entry *) (disk + (1024 * par_block_index) + total_rec_len);
	}
	
	//check theres enough space to place the new dir entry
	struct ext2_dir_entry *newdir;
	int remaining_rec_len = sizeof(curdir) + curdir->name_len; // Actual size of curdir
	remaining_rec_len += 4 - (remaining_rec_len % 4); // Add padding
	remaining_rec_len = 1024 - remaining_rec_len - total_rec_len; // Remaining space
	if (remaining_rec_len < (8 + strlen(file_name))) { // Not enough space
		// Allocate new block
		int newblock;
		for (newblock = 22; newblock < sb->s_blocks_count; newblock++) {
			if (check_node(newblock, block_bitmap) == 0) {
				break;
			}
		}
		if (newblock == sb->s_blocks_count) {
			fprintf(stderr, "No more blocks available.");
			exit(1);
		}
		set_node(newblock, block_bitmap);
		sb->s_free_blocks_count--;
		parent_node->i_block[i] = newblock; // i should still hold the first block space unused by parent
		parent_node->i_blocks += 2; // One more block being added
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
	newdir->name_len = strlen(file_name);
	newdir->file_type = EXT2_FT_REG_FILE;
	strncpy(newdir->name, file_name, newdir->name_len);

	free(dup); //free dup variable
	free(virtual_path); // free the virtual path
	close(fd);
	return 0;
}

