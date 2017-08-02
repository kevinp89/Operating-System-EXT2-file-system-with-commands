
#include<stdio.h>
#include<string.h>
#include<unistd.h>
#include<stdlib.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<sys/mman.h>
#include "ext2.h"

unsigned char get_dir_type(unsigned char file_type);
unsigned char get_inode_type(struct ext2_inode *ip);

//file type checkers
unsigned char get_dir_type(unsigned char file_type){
	if (file_type == EXT2_FT_REG_FILE){
		return 'f';
	} else if (file_type == EXT2_FT_DIR){
		return 'd';	
	} else if (file_type == EXT2_FT_SYMLINK){
		return 'l';	
	} else {
		return '?';	
	}
}

//get the type of the input inode in char form
unsigned char get_inode_type(struct ext2_inode *ip){
	if (S_ISREG(IP->i_mode){
		return 'f';	
	} else if (S_ISDIR(ip->i_mode)){
		return 'd';	
	} else if (S_ISLNK(ip->i_mode)){
		return 'l';	
	} else {
		return '?';	
	}
}


