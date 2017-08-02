/*
 * Takes two arguments:
 * First: name of an ext2 formatted disk.
 * Second: absolute path to a file or link on that disk.
 *
 * The program is the opposite of rm, restoring the specified file that has
 * been previously removed. If the file does not exist or if it is a directory,
 * return the correct error.
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
#include "ext2_utils.c"
#include<time.h>

unsigned char *disk;
struct ext2_super_block *sb;
struct ext2_group_desc *desc;

int align(int val) {
	if (val % 4 == 0) {
		return val;
	} else {
	    return val + 4 - (val % 4);
	}
}
/* remove the last / and get the last file name */
char *get_last_file_name(char *path){


    //take out the last / and get the file name (could be a dir)
    if (path[strlen(path) -1] == '/') path[strlen(path)-1] = '\0';

    // get the file name that is to be linked (first file)
    int i;
    int file_len1 = 0;
    for (i = strlen(path); path[i] != '/'; i--) {
        file_len1++;
        if (file_len1 > 255) {
            fprintf(stderr, "File name too large.\n");
            exit(1);
        }
    }

    //copy the new file name
    char *file_name = malloc(sizeof(char) * file_len1);
    strcpy(file_name, path + i + 1);

    return file_name;
}

int main(int argc, char **argv){

    if(argc != 3){
        fprintf(stderr, "Usage: ./ext2_restore [disk] [path]\n");
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
    //get the bitmap
    unsigned char *map = (unsigned char *)(disk + 1024 * desc->bg_block_bitmap);
    //get the inode map
    unsigned char *imap = (unsigned char *)(disk + 1024 * desc->bg_inode_bitmap);
    //get the parent index
    int parent_inode_index = check_parent(disk, argv[2]);
    if (parent_inode_index == -1) { // Directory not found.
        fprintf(stderr, "File does not exist.");
        exit(ENOENT);
    }

    if (argv[2][strlen(argv[2])-1] == '/') {
        fprintf(stderr, "Invalid filename.");
        exit(ENOENT);
    }
    //get the name of the file trying to restore
    char *file_name = get_last_file_name(argv[2]);

    //get the parent node
    struct ext2_inode *parent = (struct ext2_inode *)((disk + 1024 * desc->bg_inode_table) +
                                                      (sb->s_inode_size * parent_inode_index));

    /* check gaps */
    /*
     * look for any dir_entry that, when subtracting its  real space, might
     * contain another dir_entry with the original filename
     */
    int i;
    int cur_rec_len;
	char hit_name[256];
    struct ext2_dir_entry *cur_dir;
    struct ext2_dir_entry *poss_hit;
    struct ext2_inode *found_node;
    for (i = 0; parent->i_block[i] != 0; i++) {
        for (cur_rec_len = 0; cur_rec_len < EXT2_BLOCK_SIZE;) {
            cur_dir = (struct ext2_dir_entry *) (disk + (EXT2_BLOCK_SIZE *
                                                         parent->i_block[i]) +
                                                 cur_rec_len);
            if ((strcmp(cur_dir->name, ".") == 0)) {
                cur_rec_len += cur_dir->rec_len;
                continue;
            }
            int check = align(8 + cur_dir->name_len);
            if (check < cur_dir->rec_len) {
                //possible hit get the entry and check name and i_dtime.
                poss_hit = (struct ext2_dir_entry *) (disk +
						(EXT2_BLOCK_SIZE * parent->i_block[i]) +
                        ((cur_rec_len + check)));

                strcpy(hit_name, poss_hit->name);

                //check if name equal
                if (strcmp(hit_name, file_name) == 0) {
                    //check if inode isn't used up
                    if (check_node(poss_hit->inode - 1, imap)) {
                        fprintf(stderr, "Cannot restore File\n");
                        exit(1);
                    }

                    //get the inode
                    found_node = (struct ext2_inode *) 
						(disk + (1024 * desc->bg_inode_table) +
                            (sizeof(struct ext2_inode) * (poss_hit->inode-1)));

                    //check if inode has been overwritten.
                    if (found_node->i_dtime == 0) {
                        fprintf(stderr, "Cannot restore File\n");
                        exit(1);
                    }
                    //set deletion time to 0
                    found_node->i_dtime = 0;

                    //check blocks
                    int x;
                    for (x = 0; found_node->i_block[x] != 0; x++) {
                        //block was allocated
                        if (check_node(found_node->i_block[x] - 1, map)) {
                            fprintf(stderr, "Cannot restore File\n");
                            exit(1);
                        } else {
                            //block wasn't allocated, set it
                            set_node(found_node->i_block[x] - 1, map);
                            adjust_free_blocks(-1, sb, desc);
                        }
                    }
                    //mark the bit in the map
                    set_node(poss_hit->inode - 1, imap);
					adjust_free_inodes(-1, sb, desc);
					found_node->i_links_count = 1;
                    cur_dir->rec_len = check;
                    free(file_name);

                    return 0;
                }
            }
            //incr the cur len
            cur_rec_len += cur_dir->rec_len;
			memset(hit_name, 0, sizeof(hit_name));
        }
    }
    fprintf(stderr, "Cannot restore File\n");
    free(file_name);
    return 1;
}
