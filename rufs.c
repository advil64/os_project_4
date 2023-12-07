/*
 *  Copyright (C) 2023 CS416 Rutgers CS
 *	Tiny File System
 *	File:	rufs.c
 *
 */

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <limits.h>

#include "block.h"
#include "rufs.h"

char diskfile_path[PATH_MAX];

// Declare your in-memory data structures here
struct superblock *rufs_superblock;
struct dirent *rufs_dirent;

bitmap_t inode_bmap;
bitmap_t db_bmap;

/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {

	// Step 1: Read inode bitmap from disk
	
	// Step 2: Traverse inode bitmap to find an available slot

	// Step 3: Update inode bitmap and write to disk 

	return 0;
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {

	// Step 1: Read data block bitmap from disk
	
	// Step 2: Traverse data block bitmap to find an available slot

	// Step 3: Update data block bitmap and write to disk 

	return 0;
}

/* 
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {

  // Step 1: Get the inode's on-disk block number

  // Step 2: Get offset of the inode in the inode on-disk block

  // Step 3: Read the block from disk and then copy into inode structure

	return 0;
}

int writei(uint16_t ino, struct inode *inode) {

	// Step 1: Get the block number where this inode resides on disk
	
	// Step 2: Get the offset in the block where this inode resides on disk

	// Step 3: Write inode to disk 

	return 0;
}


/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {

  // Step 1: Call readi() to get the inode using ino (inode number of current directory)

  // Step 2: Get data block of current directory from inode

  // Step 3: Read directory's data block and check each directory entry.
  //If the name matches, then copy directory entry to dirent structure

	return 0;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and check each directory entry of dir_inode
	
	// Step 2: Check if fname (directory name) is already used in other entries

	// Step 3: Add directory entry in dir_inode's data block and write to disk

	// Allocate a new data block for this directory if it does not exist

	// Update directory inode

	// Write directory entry

	return 0;
}

int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and checks each directory entry of dir_inode
	
	// Step 2: Check if fname exist

	// Step 3: If exist, then remove it from dir_inode's data block and write to disk

	return 0;
}

/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
	
	// Step 1: Resolve the path name, walk through path, and finally, find its inode.
	// Note: You could either implement it in a iterative way or recursive way

	return 0;
}

/* 
 * Make file system
 */
int rufs_mkfs() {

	// Call dev_init() to initialize (Create) Diskfile
	dev_init(diskfile_path);

	// write superblock information

	rufs_superblock = (struct superblock *) malloc(BLOCK_SIZE);
	rufs_superblock->magic_num  = MAGIC_NUM;
	rufs_superblock->max_inum = MAX_INUM;
	rufs_superblock->max_dnum = MAX_DNUM;
	rufs_superblock->i_bitmap_blk = 1;
	rufs_superblock->d_bitmap_blk = 2;
	rufs_superblock->i_start_blk = 3;
	rufs_superblock->d_start_blk = 3 + INODE_BLOCKS;
    bio_write(0, rufs_superblock);

	// initialize inode bitmap
    inode_bmap = (bitmap_t)malloc(BLOCK_SIZE);
    
	// initialize data block bitmap
    db_bmap = (bitmap_t)malloc(BLOCK_SIZE);

	// update bitmap information for root directory
    set_bitmap(inode_bmap, 0);    
    bio_write(rufs_superblock->i_bitmap_blk, inode_bmap) ;

    set_bitmap(db_bmap, 0);
    bio_write(rufs_superblock->d_bitmap_blk, db_bmap);

	// update inode for root directory
    struct inode *root_inode = (struct inode *)malloc(BLOCK_SIZE);
    root_inode->ino = 0;
	root_inode->valid = 1;
	root_inode->type = 1; // directory
	root_inode->link = 0;
	root_inode->direct_ptr[0] = rufs_superblock->d_bitmap_blk;
	root_inode->indirect_ptr[0] = 0;
    root_inode->direct_ptr[1] = 0;
	
    // inode attributes
    struct stat *root_inode_stat = (struct stat *)malloc(sizeof(struct stat));
    root_inode_stat->st_mode = S_IFDIR | 0755;
    root_inode_stat->st_nlink = 1;
    root_inode_stat->st_blksize = BLOCK_SIZE;
    root_inode_stat->st_blocks = 1;
    time(&root_inode_stat->st_mtime);
    time(&root_inode_stat->st_atime);
    root_inode->vstat = *root_inode_stat;

    //save root INODE
    bio_write(rufs_superblock->i_start_blk, root_inode);
    free(root_inode_stat);
    free(root_inode);

    // directory entry for root directory
    struct dirent *root_dir = (struct dirent *)malloc(BLOCK_SIZE);
    root_dir->ino = 0; //inode is the first one
    root_dir->valid = 1;

    char dir_path[2] = { '.', '\0' };
    strncpy(root_dir->name, dir_path, 2);

    bio_write(rufs_superblock->d_start_blk, root_dir);
	free(root_dir);
    free(rufs_superblock);

	return 0;
}


/* 
 * FUSE file operations
   
 */

//	@author - Taj
static void *rufs_init(struct fuse_conn_info *conn) {

	// Step 1a: If disk file is not found, call mkfs
	// diskfile_path = -1
	if(dev_open(diskfile_path) == -1) 
	{
		//call mkfs
		rufs_mkfs();
		return NULL;
	}
	

	// Step 1b: If disk file is found, just initialize in-memory data structures
	// and read superblock from disk
	rufs_superblock = (struct superblock *) malloc(BLOCK_SIZE);
	bio_read(0, rufs_superblock);
	inode_bmap = (bitmap_t) malloc(BLOCK_SIZE);
	bio_read(rufs_superblock->i_bitmap_blk, inode_bmap);
	db_bmap = (bitmap_t) malloc(BLOCK_SIZE); 
	bio_read(rufs_superblock->d_bitmap_blk, db_bmap);

	return NULL;
}


// called when RUFS is unmounted. in this function de-allocate in-memory file system
// data structures, and close the flat file (our "disk")
static void rufs_destroy(void *userdata) {

	// Step 1: De-allocate in-memory data structures
	/*
		- superblock
		- inodes
		- bitmap
	*/

	free(inode_bmap);
	free(db_bmap);
	free(rufs_superblock);

	// Step 2: Close diskfile
	dev_close();

}

static int rufs_getattr(const char *path, struct stat *stbuf) {

	// Step 1: call get_node_by_path() to get inode from path

	// Step 2: fill attribute of file into stbuf from inode

		stbuf->st_mode   = S_IFDIR | 0755;
		stbuf->st_nlink  = 2;
		time(&stbuf->st_mtime);

	return 0;
}

static int rufs_opendir(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	// Step 2: If not find, return -1

    return 0;
}

static int rufs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	// Step 2: Read directory entries from its data blocks, and copy them to filler

	return 0;
}


static int rufs_mkdir(const char *path, mode_t mode) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name

	// Step 2: Call get_node_by_path() to get inode of parent directory

	// Step 3: Call get_avail_ino() to get an available inode number

	// Step 4: Call dir_add() to add directory entry of target directory to parent directory

	// Step 5: Update inode for target directory

	// Step 6: Call writei() to write inode to disk
	

	return 0;
}

static int rufs_rmdir(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name

	// Step 2: Call get_node_by_path() to get inode of target directory

	// Step 3: Clear data block bitmap of target directory

	// Step 4: Clear inode bitmap and its data block

	// Step 5: Call get_node_by_path() to get inode of parent directory

	// Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory

	return 0;
}

static int rufs_releasedir(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name

	// Step 2: Call get_node_by_path() to get inode of parent directory

	// Step 3: Call get_avail_ino() to get an available inode number

	// Step 4: Call dir_add() to add directory entry of target file to parent directory

	// Step 5: Update inode for target file

	// Step 6: Call writei() to write inode to disk

	return 0;
}

static int rufs_open(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	// Step 2: If not find, return -1

	return 0;
}

static int rufs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {

	// Step 1: You could call get_node_by_path() to get inode from path

	// Step 2: Based on size and offset, read its data blocks from disk

	// Step 3: copy the correct amount of data from offset to buffer

	// Note: this function should return the amount of bytes you copied to buffer
	return 0;
}

static int rufs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	// Step 1: You could call get_node_by_path() to get inode from path

	// Step 2: Based on size and offset, read its data blocks from disk

	// Step 3: Write the correct amount of data from offset to disk

	// Step 4: Update the inode info and write it to disk

	// Note: this function should return the amount of bytes you write to disk
	return size;
}

static int rufs_unlink(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name

	// Step 2: Call get_node_by_path() to get inode of target file

	// Step 3: Clear data block bitmap of target file

	// Step 4: Clear inode bitmap and its data block

	// Step 5: Call get_node_by_path() to get inode of parent directory

	// Step 6: Call dir_remove() to remove directory entry of target file in its parent directory

	return 0;
}

static int rufs_truncate(const char *path, off_t size) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_release(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int rufs_flush(const char * path, struct fuse_file_info * fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_utimens(const char *path, const struct timespec tv[2]) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}


static struct fuse_operations rufs_ope = {
	.init		= rufs_init,
	.destroy	= rufs_destroy,

	.getattr	= rufs_getattr,
	.readdir	= rufs_readdir,
	.opendir	= rufs_opendir,
	.releasedir	= rufs_releasedir,
	.mkdir		= rufs_mkdir,
	.rmdir		= rufs_rmdir,

	.create		= rufs_create,
	.open		= rufs_open,
	.read 		= rufs_read,
	.write		= rufs_write,
	.unlink		= rufs_unlink,

	.truncate   = rufs_truncate,
	.flush      = rufs_flush,
	.utimens    = rufs_utimens,
	.release	= rufs_release
};


int main(int argc, char *argv[]) {
	int fuse_stat;

	getcwd(diskfile_path, PATH_MAX);
	strcat(diskfile_path, "/DISKFILE");

	fuse_stat = fuse_main(argc, argv, &rufs_ope, NULL);

	return fuse_stat;
}

