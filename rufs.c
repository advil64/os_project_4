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
 * Traverse the inode bitmap to find an available inode, set this inode number
 * in the bitmap and return this inode number.
 */
int get_avail_ino()
{
    // Step 1: Read inode bitmap from disk
    int read_status = bio_read(rufs_superblock->i_bitmap_blk, inode_bmap);
    if (read_status < 0)
    {
        // read error
        return -1;
    }

    // Step 2: Traverse inode bitmap to find an available slot
    for (int i = 0; i < MAX_INUM; i++)
    {
        if (get_bitmap(inode_bmap, i) == 0)
        {
            // Step 3: Update inode bitmap and write to disk
            set_bitmap(inode_bmap, i);
            bio_write(rufs_superblock->i_bitmap_blk, inode_bmap);

            // return inode number.
            return i;
        }
    }

    // this means that there is no available slot
    return -1;
}

/*
 * Get available data block number from bitmap
 */
int get_avail_blkno()
{

    // Step 1: Read data block bitmap from disk
    int read_status = bio_read(rufs_superblock->d_bitmap_blk, db_bmap);
    if (read_status < 0)
    {
        // read error
        return -1;
    }

    // Step 2: Traverse data block bitmap to find an available slot
    for (int i = 0; i < MAX_DNUM; i++)
    {
        if (get_bitmap(db_bmap, i) == 0)
        {
            // Step 3: Update data block bitmap and write to disk
            set_bitmap(db_bmap, i);
            bio_write(rufs_superblock->d_bitmap_blk, db_bmap);
            // set this data block in the bitmap and returnthis data block number
            return rufs_superblock->d_start_blk + i;
        }
    }

    // this means that there is no available slot
    return -1;
}

/*
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode)
{

    // Step 1: Get the inode's on-disk block number
    int block_num = rufs_superblock->i_start_blk + (ino / INODE_BLOCKS);

    // Step 2: Get offset of the inode in the inode on-disk block
    int offset_num = ino % INODE_BLOCKS;

    // Step 3: Read the block from disk
    struct inode *inode_block = (struct inode *)malloc(BLOCK_SIZE);
    bio_read(block_num, (void *)inode_block);

    // and then copy into inode structure
    inode_block = inode_block + offset_num;
    *inode = *inode_block;

    free(inode_block - offset_num);
    return 0;
}

int writei(uint16_t ino, struct inode *inode)
{

    // Step 1: Get the block number where this inode resides on disk
    int block_num = rufs_superblock->i_start_blk + (ino / INODE_BLOCKS);

    // Step 2: Get the offset in the block where this inode resides on disk
    int offset_num = ino % INODES_PER_BLOCK;

    // Step 3: Write inode to disk
    struct inode *inode_block = (struct inode *)malloc(BLOCK_SIZE);

    bio_read(block_num, (void *)inode_block);
    inode_block = inode_block + offset_num;
    *inode_block = *inode;
    inode_block = inode_block - offset_num;
    bio_write((const int)block_num, (const void *)inode_block);

    free(inode_block);
    return 0;
}

/*
 * This function takes the inode number of the current directory, the file or sub-directory name and the length you want to
 * lookup as inputs, and then reads all direct entries of the current directory to see if the desired file or sub-directory
 * exists. If it exists, then put it into *struct dirent dirent
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent)
{

    // Step 1: Call readi() to get the inode using ino (inode number of current directory)
    struct inode *curr_inode = (struct inode *)malloc(sizeof(struct inode));
    readi(ino, curr_inode);

    // Step 2: Get data block of current directory from inode
    struct dirent *dir_block = (struct dirent *)calloc(1, BLOCK_SIZE);

    // Step 3: Read directory's data block and check each directory entry.
    // If the name matches, then copy directory entry to dirent structure
    int i, j = 0;
    for (i = 0; i < 16; i++)
    {
        // Check if data block is valid
        if (curr_inode->direct_ptr[i] == 0)
        {
            break;
        }

        bio_read(curr_inode->direct_ptr[i], dir_block);

        // Loop through all dirents in block
        for (j = 0; j < BLOCK_SIZE / sizeof(struct dirent); j++)
        {
            if (strcmp(fname, dir_block->name) == 0)
            {
                *dirent = *dir_block;
                free(curr_inode);
                free(dir_block - j);
                return 0;
            }
            dir_block++;
        }
    }

    free(curr_inode);
    free(dir_block - j);
    return -1;
}

/*
 * In this function, you would add code to add a new directory entry. This function takes as input the current directory's inode stucture,
 * the inode number to put in the directory entry, as well as the name to put into the directory entry. The function then writes a new directory
 * entry with the given inode number and name in the current directory's data blocks.
 */
int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len)
{

    // Step 1: Read dir_inode's data block and check each directory entry of dir_inode
    struct dirent *dir_block = (struct dirent *)calloc(1, BLOCK_SIZE);

    int i, j = 0;
    for (i = 0; i < 16; i++)
    {
        // Check if data block is valid
        if (dir_inode.direct_ptr[i] == 0)
        {
            break;
        }

        bio_read(dir_inode.direct_ptr[i], dir_block);

        // Step 2: Check if fname (directory name) is already used in other entries
        for (j = 0; j < BLOCK_SIZE / sizeof(struct dirent); j++)
        {
            if (strcmp(fname, dir_block->name) == 0)
            {
                return -1;
            }
            dir_block++;
        }
    }
    dir_block -= j;
    free(dir_block);

    // Step 3: Add directory entry in dir_inode's data block and write to disk
    dir_block = (struct dirent *)calloc(1, BLOCK_SIZE);
    for (i = 0; i < 16; i++)
    {

        // Allocate a new data block for this directory if it does not exist
        if (dir_inode.direct_ptr[i] == 0)
        {
            dir_inode.direct_ptr[i] = get_avail_blkno();
            bio_write(dir_inode.direct_ptr[i], (void *)dir_block);
            dir_inode.vstat.st_blocks++;
        }

        bio_read(dir_inode.direct_ptr[i], dir_block);

        for (j = 0; j < BLOCK_SIZE / sizeof(struct dirent); j++)
        {
            // We found a free directory entry
            if (dir_block->valid == 0)
            {
                dir_block->ino = f_ino;
                strncpy(dir_block->name, fname, name_len + 1);
                dir_block->valid = 1;
                dir_block->len = name_len;

                // Update directory inode
                struct inode *updated_inode = (struct inode *)malloc(sizeof(struct inode));
                *updated_inode = dir_inode;

                // Update vstat of the new inode
                struct stat *updated_inode_stat = (struct stat *)malloc(sizeof(struct stat));
                updated_inode_stat->st_mode = S_IFDIR | 0755;
                updated_inode_stat->st_blksize = BLOCK_SIZE;
                updated_inode_stat->st_blocks = i + 1;
                time(&updated_inode_stat->st_mtime);
                time(&updated_inode_stat->st_atime);
                updated_inode->vstat = *updated_inode_stat;

                // Write directory entry
                writei(updated_inode->ino, updated_inode);
                dir_block -= j;
                bio_write(dir_inode.direct_ptr[i], (const void *)dir_block);

                free(dir_block);
                free(updated_inode);
                free(updated_inode_stat);

                return 0;
            }
            dir_block++;
        }
    }

    return -1; // out of space
}

int dir_remove(struct inode dir_inode, const char *fname, size_t name_len)
{

    // Step 1: Read dir_inode's data block and checks each directory entry of dir_inode

    // Step 2: Check if fname exist

    // Step 3: If exist, then remove it from dir_inode's data block and write to disk

    return 0;
}

/*
 * This is the actual namei function which follows a pathname until a terminal point is found. To implement this function use
 * the path, the inode number of the root of this path as input, then call dir_find() to lookup each component in the path,
 * and finally read the inode of the terminal point to "struct inode *inode"
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode)
{

    // Step 1: Resolve the path name, walk through path, and finally, find its inode.
    // Note: You could either implement it in a iterative way or recursive way
    char *DELIM = (char[2]){'/', '\0'};

    // Check for base case
    if (path == NULL || strlen(path) == 0)
    {
        return 0;
    }

    // Check if the path is the root
    if (strcmp(path, DELIM) == 0)
    {
        // Handle root case
        readi(0, inode);
        return 0;
    }

    // Get the first token in the given path
    char *path_copy = strdup(path);
    char *token = strtok(path_copy, DELIM);
    char *remaining_path = strdup(path + strlen(token) + 1);

    // Allocate memory for the entry
    struct dirent *entry = (struct dirent *)malloc(sizeof(struct dirent));

    // check if file/subdirectory is present in its parent
    if (dir_find(ino, (const char *)token, strlen(token), entry) == -1)
    {
        return -1;
    }
    readi(entry->ino, inode);

    if (inode->type == 0 && strlen(remaining_path) != 0){
        return -1; // trying to recursively access a file as a directory
    }

    // Recursively resolve the remaining path
    free(path_copy);
    return get_node_by_path(remaining_path, entry->ino, inode);
}

/*
 * Make file system
 */
int rufs_mkfs()
{

    // Call dev_init() to initialize (Create) Diskfile
    dev_init(diskfile_path);

    // write superblock information

    rufs_superblock = (struct superblock *)calloc(1, BLOCK_SIZE);
    rufs_superblock->magic_num = MAGIC_NUM;
    rufs_superblock->max_inum = MAX_INUM;
    rufs_superblock->max_dnum = MAX_DNUM;
    rufs_superblock->i_bitmap_blk = 1;
    rufs_superblock->d_bitmap_blk = 2;
    rufs_superblock->i_start_blk = 3;
    rufs_superblock->d_start_blk = 3 + INODE_BLOCKS;
    bio_write(0, rufs_superblock);

    // initialize inode bitmap
    inode_bmap = (bitmap_t)calloc(1, BLOCK_SIZE);

    // initialize data block bitmap
    db_bmap = (bitmap_t)calloc(1, BLOCK_SIZE);

    // update bitmap information for root directory
    set_bitmap(inode_bmap, 0);
    bio_write(rufs_superblock->i_bitmap_blk, inode_bmap);

    set_bitmap(db_bmap, 0);
    bio_write(rufs_superblock->d_bitmap_blk, db_bmap);

    // update inode for root directory
    struct inode *root_inode = (struct inode *)malloc(BLOCK_SIZE);
    root_inode->ino = 0;
    root_inode->valid = 1;
    root_inode->type = 1; // directory
    root_inode->link = 0;
    root_inode->direct_ptr[0] = rufs_superblock->d_start_blk;
    root_inode->indirect_ptr[0] = 0;

    // inode attributes
    struct stat *root_inode_stat = (struct stat *)malloc(sizeof(struct stat));
    root_inode_stat->st_mode = S_IFDIR | 0755;
    root_inode_stat->st_nlink = 1;
    root_inode_stat->st_blksize = BLOCK_SIZE;
    root_inode_stat->st_blocks = 1;
    time(&root_inode_stat->st_mtime);
    time(&root_inode_stat->st_atime);
    root_inode->vstat = *root_inode_stat;

    // save root INODE
    bio_write(rufs_superblock->i_start_blk, root_inode);
    free(root_inode_stat);
    free(root_inode);

    // directory entry for root directory
    struct dirent *root_dir = (struct dirent *)calloc(1, BLOCK_SIZE);
    root_dir->ino = 0; // inode is the first one
    root_dir->valid = 1;
    root_dir->len = 1;

    char dir_path[2] = {'.', '\0'};
    strncpy(root_dir->name, dir_path, 2);

    bio_write(rufs_superblock->d_start_blk, root_dir);
    free(root_dir);

    return 0;
}

/*
 * This function is the initialization function of RUFS. In this function, you will open a flat file (our 'disk', remember
 * the virtual memory setup) and read a superblock into memory. If the flat file does not exist (our 'disk' is not formatted),
 * it will need to call rufs_mkfs() to format our "disk" (partition the flat file into superblock region, inode region, bitmap
 * region, and data block region). You must also allocate any in-memory file system data structures that you may need.
 */
static void *rufs_init(struct fuse_conn_info *conn)
{
    // Step 1a: If disk file is not found, call mkfs
    if (dev_open(diskfile_path) == -1)
    {
        rufs_mkfs();
        return NULL;
    }

    // Step 1b: If disk file is found, just initialize in-memory data structures
    // and read superblock from disk
    rufs_superblock = (struct superblock *)malloc(BLOCK_SIZE);
    bio_read(0, rufs_superblock);
    inode_bmap = (bitmap_t)malloc(BLOCK_SIZE);
    bio_read(rufs_superblock->i_bitmap_blk, inode_bmap);
    db_bmap = (bitmap_t)malloc(BLOCK_SIZE);
    bio_read(rufs_superblock->d_bitmap_blk, db_bmap);

    return NULL;
}

// called when RUFS is unmounted. in this function de-allocate in-memory file system
// data structures, and close the flat file (our "disk")
static void rufs_destroy(void *userdata)
{

    // Step 1: De-allocate in-memory data structures
   free(rufs_superblock);
   // TODO Free inodes
    free(inode_bmap);
    free(db_bmap);
    free(rufs_superblock);

    // Step 2: Close diskfile
    dev_close();
}

/*
 * This function is called when accessing a file or directory and provides the stats of your file, such as inode permission, size,
 * number of references, and other inode information. It takes the path of a file or directory as an input. To implement this
 * function, use the input path to find the inode, and for a valid path (inode), fill information inside "struct stat *stbuf".
 * On success, the return value must be 0; otherwise, return the right error code.
 */

static int rufs_getattr(const char *path, struct stat *stbuf)
{

    // Step 1: call get_node_by_path() to get inode from path
    struct inode *input_node = (struct inode *)malloc(sizeof(struct inode));
    if (get_node_by_path(path, 0, input_node) != 0)
    {
        return -ENOENT;
    }

    // Step 2: fill attribute of file into stbuf from inode
    *stbuf = input_node->vstat;

    return 0;
}

// @author - Taj
static int rufs_opendir(const char *path, struct fuse_file_info *fi)
{

    // Step 1: Call get_node_by_path() to get inode from path
    struct inode* dir_node = (struct inode*) malloc(sizeof(struct inode));

    if(get_node_by_path(path, 0, dir_node) == 0) 
    {
        if(dir_node->valid)
        {
            free(dir_node);
            return 0;
        }
    }

    // Step 2: If not find, return -1
    free(dir_node);
    return -1;
}

static int rufs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{

    // Step 1: Call get_node_by_path() to get inode from path

    // Step 2: Read directory entries from its data blocks, and copy them to filler

    return 0;
}

/*
 * This function is called when creating a directory (mkdir command). It takes the path and mode of the directory as an input. 
 * This function will first need to separate the directory name and base name of the path. (e.g., for the path "/foo/bar/tmp", 
 * the directory name is "/foo/bar", the base name is "tmp"). It should then read the inode of the directory name and traverse 
 * its directory entries to see if there's already a directory entry whose name is base name, and if true, return a negative value; 
 * otherwise, the base name must be added as a directory. The next step is to add a new directory entry to the current directory, 
 * allocate an inode, and also update the bitmaps.
*/
static int rufs_mkdir(const char *path, mode_t mode)
{

    // Step 1: Use dirname() and basename() to separate parent directory path and target directory name
    char *dir_name = (char *)malloc(strlen(path) + 1); // +1 for null ender
    char *base_name = (char *)malloc(strlen(path) + 1);

    char *dir_copy_1 = strdup(path);
    char *base_copy_2 = strdup(path);

    strcpy(dir_name, dirname(dir_copy_1));
    strcpy(base_name, basename(base_copy_2));

    // Step 2: Call get_node_by_path() to get inode of parent directory
    struct inode *parent_inode = (struct inode *)malloc(sizeof(struct inode));
    if (get_node_by_path(dir_name, 0, parent_inode) != 0)
    {
        return -ENOENT;
    }

    // Step 3: Call get_avail_ino() to get an available inode number
    uint16_t inode_num = get_avail_ino();
    if (inode_num < 0)
    {
        return -1;
    }

    // Step 4: Call dir_add() to add directory entry of target directory to parent directory
    dir_add(*parent_inode, inode_num, (const char *)base_name, strlen(base_name));

    // Step 5: Update inode for target directory
    struct inode *directory_inode = (struct inode *)calloc(1, sizeof(struct inode));
    directory_inode->ino = inode_num;
    directory_inode->valid = 1;
    directory_inode->type = 1; //directory
    directory_inode->size = BLOCK_SIZE;

    // stat
    struct stat *inode_stat = (struct stat *)malloc(sizeof(struct stat));
    inode_stat->st_mode = S_IFDIR | 0755;
    inode_stat->st_nlink = 1;

    time(&inode_stat->st_mtime);
    inode_stat->st_blocks = 1;
    inode_stat->st_blksize = BLOCK_SIZE;
    inode_stat->st_size = BLOCK_SIZE;
    directory_inode->vstat = *inode_stat;

    // Step 6: Call writei() to write inode to disk
    writei(inode_num, directory_inode);

    free(inode_stat);
    free(directory_inode);
    free(dir_copy_1);
    free(base_copy_2);
    free(dir_name);
    free(base_name);

    return 0;
}

static int rufs_rmdir(const char *path)
{

    // For this project, you don't need to fill this function
    // But DO NOT DELETE IT!
    return 0;
}

static int rufs_releasedir(const char *path, struct fuse_file_info *fi)
{
    // For this project, you don't need to fill this function
    // But DO NOT DELETE IT!
    return 0;
}

/*
 * This function is called when creating a file (e.g., touch command). It takes the path and mode of a file as an input.
 * This function should first separate the directory name and base name of the path. (e.g. for path "/foo/bar/a.txt", the
 * directory name is "/foo/bar", the base name is "a.txt"). It should then read the inode of the directory name, and traverse
 * its directory entries to see if there's already a directory entry whose name is base name, if so, then it should return a
 * negative value. Otherwise, base name is a valid file name to be added. The next step is to add a new directory entry
 * ("a.txt") using dir_add() to the current directory, allocate an inode, and update the bitmaps
 */
static int rufs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{

    // Step 1: Use dirname() and basename() to separate parent directory path and target file name
    char *dir_name = (char *)malloc(strlen(path) + 1); // +1 for null ender
    char *base_name = (char *)malloc(strlen(path) + 1);

    char *copy_1 = strdup(path);
    char *copy_2 = strdup(path);

    strcpy(dir_name, dirname(copy_1));
    strcpy(base_name, basename(copy_2));

    // Step 2: Call get_node_by_path() to get inode of parent directory
    struct inode *parent_inode = (struct inode *)malloc(sizeof(struct inode));
    if (get_node_by_path(dir_name, 0, parent_inode) != 0)
    {
        return -ENOENT;
    }

    // Step 3: Call get_avail_ino() to get an available inode number
    uint16_t inode_num = get_avail_ino();

    // Step 4: Call dir_add() to add directory entry of target file to parent directory
    dir_add(*parent_inode, inode_num, (const char *)base_name, strlen(base_name));

    // Step 5: Update inode for target file
    struct inode *file_inode = (struct inode *)calloc(1, sizeof(struct inode));
    file_inode->ino = inode_num;
    file_inode->valid = 1;
    file_inode->type = 0; // file
    file_inode->link = 0;

    // inode attributes
    struct stat *file_inode_stat = (struct stat *)malloc(sizeof(struct stat));
    file_inode_stat->st_mode = S_IFREG | 0666;
    file_inode_stat->st_nlink = 1;
    file_inode_stat->st_blksize = BLOCK_SIZE;
    file_inode_stat->st_blocks = 1;
    time(&file_inode_stat->st_mtime);
    time(&file_inode_stat->st_atime);
    file_inode->vstat = *file_inode_stat;

    // Step 6: Call writei() to write inode to disk
    writei(inode_num, file_inode);

    free(file_inode_stat);
    free(file_inode);
    free(copy_1);
    free(copy_2);
    free(dir_name);
    free(base_name);

    return 0;
}

static int rufs_open(const char *path, struct fuse_file_info *fi)
{
    // Step 1: Call get_node_by_path() to get inode from path
    struct inode *node = (struct inode *)malloc(sizeof(struct inode));

    if(get_node_by_path(path, 0, node) == 0) 
    {
        if(node->valid) 
        {
            free(node);
            return 0;
        }
    }

    // Step 2: If not find, return -1
    free(node);
    return -1;
}

/*
 * This function is the read operation's call handler. It takes the path of the file, read size and offset as input. To implement this function, 
 * read the inode of this file from the path input, get the inode, and the data blocks using the inode. Copy size bytes from the inodes data 
 * blocks starting at offset to the memory area pointed to by buffer.
*/
static int rufs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi)
{

    // Step 1: You could call get_node_by_path() to get inode from path
    struct inode *file_inode = (struct inode *)malloc(sizeof(struct inode));
    if (get_node_by_path(path, 0, file_inode) != 0)
    {
        return -ENOENT;
    }

    // Step 2: Based on size and offset, read its data blocks from disk
    int i;
    char *contents = (char *)calloc(16, BLOCK_SIZE);
    for (i = 0; i < 16; i++)
    {
        if (file_inode->direct_ptr[i] == 0)
        {
            break; // done loading the mem buffer
        }
        bio_read(file_inode->direct_ptr[i], contents);
        contents += BLOCK_SIZE;
    }

    // Step 3: copy the correct amount of data from offset to buffer
    contents -= (i * BLOCK_SIZE);
    memcpy(buffer, contents + offset, size);

    free(file_inode);
    free(contents);

    // Note: this function should return the amount of bytes you copied to buffer
    return size;
}

/*
 * This function is the write call handler. It takes the path of the file, write size, and offset as an input. To perform a write
 * operation, read the inode using the file path and using the inode, locate the data blocks and then copy size bytes from the memory
 * area pointed by buffer to the file's data blocks starting at offset.
 */
static int rufs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi)
{
    // Step 1: You could call get_node_by_path() to get inode from path
    struct inode *file_inode = (struct inode *)malloc(sizeof(struct inode));
    if (get_node_by_path(path, 0, file_inode) != 0)
    {
        return -ENOENT;
    }

    // Step 2: Based on size and offset, read its data blocks from disk
    int i;
    char *contents = (char *)calloc(16, BLOCK_SIZE);
    for (i = 0; i < 16; i++)
    {
        if (file_inode->direct_ptr[i] == 0)
        {
            break; // done loading the mem buffer
        }
        bio_read(file_inode->direct_ptr[i], contents);
        contents += BLOCK_SIZE;
    }

    // Step 3: Write the correct amount of data from offset to disk
    int bytes_to_write = offset + size;
    contents -= (i * BLOCK_SIZE);
    memcpy(contents + offset, buffer, size);

    // compare the blocks to be written vs blocks that already exist
    int blocks_to_iter = (file_inode->vstat.st_blocks > ((bytes_to_write - 1) / BLOCK_SIZE + 1)) ? file_inode->vstat.st_blocks : bytes_to_write / BLOCK_SIZE;

    // Step 4: Update the inode info and write it to disk
    for (i = 0; i < blocks_to_iter; i++)
    {
        if (file_inode->direct_ptr[i] == 0)
        {
            file_inode->direct_ptr[i] = get_avail_blkno();
        }
        bio_write(file_inode->direct_ptr[i], (void *)contents);
        contents += BLOCK_SIZE;
    }

    file_inode->vstat.st_blocks = blocks_to_iter;
    file_inode->vstat.st_size = file_inode->vstat.st_blksize * blocks_to_iter;
    writei(file_inode->ino, file_inode);

    free(file_inode);
    free(contents - (i * BLOCK_SIZE));

    // Note: this function should return the amount of bytes you write to disk
    return size;
}

static int rufs_unlink(const char *path)
{

    // For this project, you don't need to fill this function
    // But DO NOT DELETE IT!
    return 0;
}

static int rufs_truncate(const char *path, off_t size)
{
    // For this project, you don't need to fill this function
    // But DO NOT DELETE IT!
    return 0;
}

static int rufs_release(const char *path, struct fuse_file_info *fi)
{
    // For this project, you don't need to fill this function
    // But DO NOT DELETE IT!
    return 0;
}

static int rufs_flush(const char *path, struct fuse_file_info *fi)
{
    // For this project, you don't need to fill this function
    // But DO NOT DELETE IT!
    return 0;
}

static int rufs_utimens(const char *path, const struct timespec tv[2])
{
    // For this project, you don't need to fill this function
    // But DO NOT DELETE IT!
    return 0;
}

static struct fuse_operations rufs_ope = {
    .init = rufs_init,
    .destroy = rufs_destroy,

    .getattr = rufs_getattr,
    .readdir = rufs_readdir,
    .opendir = rufs_opendir,
    .releasedir = rufs_releasedir,
    .mkdir = rufs_mkdir,
    .rmdir = rufs_rmdir,

    .create = rufs_create,
    .open = rufs_open,
    .read = rufs_read,
    .write = rufs_write,
    .unlink = rufs_unlink,

    .truncate = rufs_truncate,
    .flush = rufs_flush,
    .utimens = rufs_utimens,
    .release = rufs_release};

int main(int argc, char *argv[])
{
    int fuse_stat;

    getcwd(diskfile_path, PATH_MAX);
    strcat(diskfile_path, "/DISKFILE");

    fuse_stat = fuse_main(argc, argv, &rufs_ope, NULL);

    return fuse_stat;
}
