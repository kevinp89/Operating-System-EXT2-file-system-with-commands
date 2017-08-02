/*
 * Takes three arguments:
 * First: the name of an ext2 formatted disk.
 * Second: absolute path on your ext2 (source file)
 * Third: absolute path on your ext2 (destination path)
 *
 * This program should work like ln, creating a link from the first specified
 * file to the second specified path.
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
#include "errno.h"
#include<sys/mman.h>
#include<time.h>
#include "ext2_utils.c"

unsigned char *disk;
struct ext2_super_block *sb;
struct ext2_group_desc *desc;




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

int check_path(char *path){
    if(path[0] != '/'){
        return 1;
    } else {
        return 0;
    }

}


int main(int argc, char **argv){
    //arguments check
    if(argc < 4){
        fprintf(stderr, "Usage: %s ./ext2_ln [disk] [Src Path] [target "
                        "path]\n",
                argv[0]);
        exit(1);
    }

    /* get args */
    char *disk_img = NULL; //disk img path
    char *src_path = NULL; //src path in virtual disk
    char *target_path = NULL; //target path in virtual disk
    int s_link_flag=0; //symlink flag

    /* get the args and store in vars */
    int c;
    if ((c = getopt(argc, argv, "s")) != -1) {
            if (c == 's') {
                s_link_flag = 1;
            }
        int a;
        for(a=optind; a<argc; a++){

            switch (a) {
                case 2:
                    disk_img = argv[a];
                    break;
                case 3:
                    src_path = argv[a];
                    break;
                case 4:
                    target_path = argv[a];
                    break;
                default:
                    break;
            }
        }
    } else {
        disk_img = argv[1];
        src_path = argv[2];
        target_path = argv[3];
    }


    //opening the disk
    int fd = open(disk_img, O_RDWR);
    if(!fd){
        fprintf(stderr, "Disk image '%s' not found\n", argv[1]);
        exit(1);
    }

    /**check if paths are absolute */
    //check if virtual path 1 is valid
    int check = check_path(src_path);
    if(check == 1){
        fprintf(stderr, "Please provide absolute path for virtual path 1");
        exit(1);
    }

    //check virtual path 2 is absolute or not.
    int check2 = check_path(target_path);
    if(check2 == 1){
        fprintf(stderr, "Please provide absolute path for virtual path");
        exit(1);
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

    unsigned char *bmap = (unsigned char *)(disk + 1024 * desc->bg_block_bitmap);

    //check if the file paths exists, if so get the parent
    int parent_index_1 = check_parent(disk, src_path);
    if(parent_index_1 == -1){
        fprintf(stderr, "'%s' No such file or directory\n", src_path);
        exit(ENOENT);
    }
    //get the parent inode
    struct ext2_inode *parent_node1 = (struct ext2_inode *) (
            (disk+EXT2_BLOCK_SIZE * desc->bg_inode_table) +
            (sb->s_inode_size * parent_index_1));


    int parent_index_2 = check_parent(disk, target_path);
    if(parent_index_2 == -1){
        fprintf(stderr, "No such file or directory");
        exit(ENOENT);
    }

    /** get the last names from both files */

    char *file_name1 = get_last_file_name(src_path);
    char *file_name2 = get_last_file_name(target_path);

    //check if first file is a directory or not
    int check_file = search_directories(disk, parent_node1, file_name1, 1);
    if(check_file != -1){
        fprintf(stderr, "Give file: '%s' is a directory", file_name1);
        exit(EISDIR);
    }

    //for second one, get parent to check if file exists
    //get the parent node
    struct ext2_inode *parent_node2 = (struct ext2_inode *) (
            (disk+EXT2_BLOCK_SIZE * desc->bg_inode_table) +
            (sb->s_inode_size * parent_index_2));

    int check_dir_exists = search_directories(disk,parent_node2, file_name2, 1);
    if(check_dir_exists != -1){
        fprintf(stderr, "File: '%s' already exists", file_name2);
        exit(EEXIST);
    }

    int check_file_exists = search_directories(disk,parent_node2, file_name2, 0);
    if(check_file_exists != -1){
        fprintf(stderr, "File: '%s' already exists", file_name2);
        exit(EEXIST);
    }

    //get the inode for first file name
    unsigned int inode_indx1 = search_directories(disk, parent_node1,
                                                  file_name1, 0);
    struct ext2_inode *inode1 = (struct ext2_inode *) ((disk +
            EXT2_BLOCK_SIZE * desc->bg_inode_bitmap) +
            (sb->s_inode_size * inode_indx1));

    /* if -s is provided, create new inode */
    int inode;
    struct ext2_inode *new_inode;
    if(s_link_flag){
        // Finding a free inode and allocating (set to 1)
        unsigned char *imap = (unsigned char *)(disk + 1024 *
                                                       desc->bg_inode_bitmap);

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
        char *data_block;
        // Actual allocation.
        set_node(inode, imap);
        set_node(bnode, bmap);

        adjust_free_blocks(-1, sb, desc);
        adjust_free_inodes(-1, sb, desc);
        // Writing data to the ext2_inode struct in the inode index.
        new_inode = (struct ext2_inode *)((disk + 1024 * desc->bg_inode_table)
                                                             + (sb->s_inode_size * (inode)));
        new_inode->i_mode = EXT2_S_IFLNK;
        new_inode->i_uid = 0;
        new_inode->i_size = strlen(src_path); //size is the same as src node
        new_inode->i_ctime = (unsigned int) time(0);
        new_inode->i_dtime = 0;
        new_inode->i_gid = 0;
        new_inode->i_blocks = 2;
        new_inode->osd1 = 0;
        new_inode->i_block[0] = bnode; //change??
        new_inode->i_generation = 0;
        new_inode->i_file_acl = 0;
        new_inode->i_dir_acl = 0;
        new_inode->i_faddr = 0;
        new_inode->i_links_count = 2; // Itself and from parent.
        parent_node2->i_links_count++; // Parent gets one more link from parent dir in new directory. ???

//        read in src path into data block
        data_block = (char*)(disk + EXT2_BLOCK_SIZE *
                                          new_inode->i_block[0]);
        strcpy(data_block, src_path);

    }

    // Adding new inode/directory entry into the parent block.
    int i;
    int par_block_index;
    for (i = 0; parent_node2->i_block[i] != 0; i++) {
        par_block_index = parent_node2->i_block[i];
    }
    int total_rec_len;
    struct ext2_dir_entry *curdir = (struct ext2_dir_entry *) (disk + (1024 * par_block_index));
    // When this for loop exits, curdir should be the last directory
    for (total_rec_len = 0; total_rec_len + curdir->rec_len < 1024;) {
        total_rec_len += curdir->rec_len;
        curdir = (struct ext2_dir_entry *) (disk + (1024 * par_block_index) + total_rec_len);
    }
    // Check there's enough space to place the new dir entry in the current block
    // Reuse total_rec_len to find remaining bytes if curdir was reduced to proper size
    struct ext2_dir_entry *newdir;
    int remaining_rec_len = sizeof(curdir) + curdir->name_len; // Actual size of curdir
    remaining_rec_len += 4 - (remaining_rec_len % 4); // Add padding
    remaining_rec_len = 1024 - remaining_rec_len - total_rec_len; // Remaining space
    if (remaining_rec_len < (8 + strlen(file_name2))) { // Not enough space
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
        sb->s_free_blocks_count--;
        parent_node2->i_block[i] = newblock; // i should still hold the first block space unused by parent
        parent_node2->i_blocks += 2; // One more block being added
        newdir = (struct ext2_dir_entry *) (disk + (1024 * newblock));
        newdir->rec_len = 1024; // Takes up the whole of the new block.
    } else { // Enough space
        // Change curdir, add new directory
        curdir->rec_len = sizeof(curdir) + curdir->name_len;
        curdir->rec_len += 4 - (curdir->rec_len % 4);
        total_rec_len += curdir->rec_len;
        newdir = (struct ext2_dir_entry *) (disk + (1024 *
                par_block_index) + total_rec_len);
        newdir->rec_len = 1024 - total_rec_len; // Takes up the rest of the block.
    }
    if(s_link_flag){
        newdir->inode = inode + 1;
        newdir->name_len = strlen(file_name2);
        newdir->file_type = EXT2_FT_SYMLINK;
        strncpy(newdir->name, file_name2, newdir->name_len);
        //no increase in the link count
    } else {
        //for the hard link
        newdir->inode = inode_indx1 + 1; // set the index to the linking index
        newdir->name_len = strlen(file_name2); //set the new name length
        newdir->file_type = EXT2_FT_REG_FILE; //set file type
        strncpy(newdir->name, file_name2,
                newdir->name_len); //set the new name
        inode1->i_links_count++; //increment the link count
    }
    return 0;
}