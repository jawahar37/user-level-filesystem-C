/*
 *  Copyright (C) 2023 CS416 Rutgers CS
 *	Tiny File System
 *	File:	rufs.c
 *
 */

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <math.h>
#include <stdarg.h>
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
#include <pthread.h>

#include "block.h"
#include "rufs.h"

char diskfile_path[PATH_MAX];

#define INODE_SIZE sizeof(struct inode)
#define INODES_PER_BLOCK (BLOCK_SIZE / INODE_SIZE)
#define DIRENT_SIZE sizeof(struct dirent)

// In-memory data structures
struct superblock *SB;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

// function declarations
void debug(char* fmt, ...);
void log_rufs(char* fmt, ...);


/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {

    // Step 1: Read inode bitmap from disk
    bitmap_t i_bitmap = (bitmap_t)malloc(BLOCK_SIZE);
    bio_read(SB->i_bitmap_blk, i_bitmap);
    
    int inode_idx;
    // Step 2: Traverse inode bitmap to find an available slot
    for (inode_idx = 0; inode_idx < SB->max_inum; inode_idx++)
    {
        if(get_bitmap( i_bitmap, inode_idx)==0)
        {
            // Step 3: Update inode bitmap and write to disk
            set_bitmap( i_bitmap, inode_idx);
            bio_write(SB->i_bitmap_blk, i_bitmap);
            free(i_bitmap);
            printf("\t- Available inode: %d.\n", inode_idx);
            return inode_idx;
        }
    }
    free(i_bitmap);
    return -1;
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {

    // Step 1: Read data block bitmap from disk
    bitmap_t d_bitmap = (bitmap_t)malloc(BLOCK_SIZE);
    bio_read(SB->d_bitmap_blk, d_bitmap);
    
    int data_idx;
    // Step 2: Traverse data block bitmap to find an available slot
    for (data_idx = 0; data_idx < SB->max_inum; data_idx++)
    {
        if(get_bitmap( d_bitmap, data_idx)==0)
        {
            // Step 3: Update data block bitmap and write to disk
            set_bitmap( d_bitmap, data_idx);
            bio_write(SB->d_bitmap_blk, d_bitmap);
            free(d_bitmap);
            printf("\t- Available datablock: %d.\n", data_idx);
            return data_idx;
        }
    }
    free(d_bitmap);
    return -1;
}

/* 
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {

    // Step 1: Get the inode's on-disk block number
    int block_num =  SB->i_start_blk + ((ino * INODE_SIZE ) / BLOCK_SIZE);

    // Step 2: Get offset of the inode in the inode on-disk block
    int offset = (ino % INODES_PER_BLOCK) * INODE_SIZE ;
    printf("\t- readi. ino: %d, block_num: %d, offset: %d\n", ino, block_num, offset);
    struct inode * inode_block = (struct inode *) malloc(BLOCK_SIZE);

    // Step 3: Read the block from disk and then copy into inode structure
    bio_read(block_num, inode_block);
    memcpy(inode, inode_block + offset , INODE_SIZE);
	free(inode_block);

    return 0;
}

int writei(uint16_t ino, struct inode *inode) {

    // Step 1: Get the block number where this inode resides on disk
    int block_num =  SB->i_start_blk + ((ino * INODE_SIZE ) / BLOCK_SIZE);

    // Step 2: Get the offset in the block where this inode resides on disk
    int offset = (ino % INODES_PER_BLOCK) * INODE_SIZE ;
    printf("\t- writei. ino: %d, block_num: %d, offset: %d\n", ino, block_num, offset);
    struct inode * inode_block = (struct inode *) malloc(BLOCK_SIZE);

    // Step 3: Write inode to disk
    bio_read(block_num, inode_block);
    memcpy(inode_block + offset, inode , INODE_SIZE);
    bio_write(block_num, inode_block);
	free(inode_block);
    
    return 0;
}


/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {
    debug("\t-- dir_find\n");
    // Step 1: Call readi() to get the inode using ino (inode number of current directory)
    struct inode dir_inode;
    readi(ino, &dir_inode);

    // Step 2: Get data block of current directory from inode
    for (int i = 0; i < 16; i++) {
        if (dir_inode.direct_ptr[i] != -1) {
            // Read a directory entries block
            struct dirent *entries = (struct dirent *)malloc(BLOCK_SIZE);
            bio_read(dir_inode.direct_ptr[i], entries);
            
            // Step 3: Read directory's data block and check each directory entry.
            for (int j = 0; j < floor((BLOCK_SIZE / DIRENT_SIZE)); j++) {
                
                //If the name matches, then copy directory entry to dirent structure
                if (entries[j].valid && strncmp(entries[j].name, fname, name_len) == 0) {
                    printf("\t- Directory %s found in %d.\n", fname, dir_inode.direct_ptr[i]);
                    
                    memcpy(dirent, &entries[j], sizeof(struct dirent));
                    free(entries);
                    return 0;
                }
            }
        }
    
    }

    return -1;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {

    // Step 1: Read dir_inode's data block and check each directory entry of dir_inode
    for (int i = 0; i < 16; i++) {
        if (dir_inode.direct_ptr[i] != -1) {
            // Read the block containing directory entries
            struct dirent *entries = (struct dirent *)malloc(BLOCK_SIZE);
            bio_read(dir_inode.direct_ptr[i], entries);
            
            // Step 2: Check if fname (directory name) is already used in other entries
            for (int j = 0; j < floor((BLOCK_SIZE / DIRENT_SIZE)); j++) {
                if (entries[j].valid && strncmp(entries[j].name, fname, name_len) == 0) {
                    printf("\t- Directory %s already exists in %d.\n", fname, dir_inode.direct_ptr[i]);

                    free(entries);
                    return -1;
                }
            }
            free(entries);
        }
    }

    int foundSlot = 0;
    // Step 3: Add directory entry in dir_inode's data block and write to disk
    for (int i = 0; i < 16; i++) {
        // check all existing pointers for a block with an empty slot
        if (dir_inode.direct_ptr[i] != -1) {
            struct dirent *entries = (struct dirent *)malloc(BLOCK_SIZE);
            bio_read(dir_inode.direct_ptr[i], entries);
            
            for (int j = 0; j < floor((BLOCK_SIZE / DIRENT_SIZE)); j++) {
                if (!entries[j].valid) {
                    foundSlot = 1;

                    entries[j].valid = 1;
                    entries[j].ino = f_ino;
                    strcpy(entries[j].name, fname);
                    break;
                }
            }
            free(entries);
        }
    }

    // Allocate a new data block for this directory if it does not exist
    if(!foundSlot) {
        for (int i = 0; i < 16; i++) {
            // check all pointers for an empty slot
            if (dir_inode.direct_ptr[i] == -1) {
                foundSlot = 1;

                int block_num = get_avail_blkno();
                dir_inode.direct_ptr[i] = block_num;

                struct dirent *entries = (struct dirent *)malloc(BLOCK_SIZE);
                memset(entries, 0, BLOCK_SIZE);
                entries[0].valid = 1;
                entries[0].ino = f_ino;
                strcpy(entries[0].name, fname);
                bio_write(block_num, entries);
            }
        }
    }

    if(!foundSlot) {
        printf("All dirent slots full in this directory.");
        return -1;
    }
    // Update directory inode
    dir_inode.size += 1;
    time(&dir_inode.vstat.st_atime);
    time(&dir_inode.vstat.st_mtime);

    // Write directory entry
    bio_write(dir_inode.ino, &dir_inode);
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
    char *token, *rest = NULL;
    char *path_copy = strdup(path);

    struct dirent dirent;
    struct inode current_inode;
    // /abc/xzy/two.txt
    // /one.txt
    while ((token = strtok_r(rest ? rest : path_copy, "/", &rest)) != NULL) {


        // Find the directory entry in the current directory
        if (dir_find(ino, token, strlen(token), &dirent) != 0) {
            free(path_copy);
            return -1; // Directory entry not found
        }

        // Read the inode of the found entry
        if (readi(dirent.ino, &current_inode) != 0) {
            free(path_copy);
            return -1; // Failed to read inode
        }

        // Update ino for the next iteration
        ino = dirent.ino;
    }

    // Copy the final inode to the provided inode structure
    memcpy(inode, &current_inode, sizeof(struct inode));
    
    free(path_copy);
    return 0; // Success
}

/* 
 * Make file system
 */
int rufs_mkfs() {
    log_rufs("--rufs_mkfs--\n");

    dev_init(diskfile_path);

    // initialize superblock
    SB = (struct superblock *)malloc(sizeof(struct superblock));
    SB->magic_num = MAGIC_NUM;
    SB->max_inum = MAX_INUM;
    SB->max_dnum = MAX_DNUM;
    SB->i_bitmap_blk = 1;
    SB->d_bitmap_blk = 2;
    SB->i_start_blk = 3;
    SB->d_start_blk = 3 + ceil((double)MAX_INUM / INODES_PER_BLOCK);
    // write superblock to disk
        
        debug("\t- Write Superblock to disk.\n");
    bio_write(0, SB);

    // initialize inode bitmap
    bitmap_t i_bitmap = (bitmap_t)malloc(BLOCK_SIZE);
    memset(i_bitmap, 0, BLOCK_SIZE);
        debug("\t- Write inode bitmap to disk.\n");
    bio_write(SB->i_bitmap_blk, i_bitmap);

    // initialize data block bitmap
    bitmap_t d_bitmap = (bitmap_t)malloc(BLOCK_SIZE);
    memset(d_bitmap, 0, BLOCK_SIZE);
        debug("\t- Write data bitmap to disk.\n");
    bio_write(SB->d_bitmap_blk, d_bitmap);

    // update bitmap information for root directory
    set_bitmap(i_bitmap, 0); // Root directory's inode is in use
    set_bitmap(d_bitmap, 0); // Root directory's data block is in use

        debug("\t- Write both bitmaps to disk.\n");
    bio_write(SB->i_bitmap_blk, i_bitmap);
    bio_write(SB->d_bitmap_blk, d_bitmap);

    // update inode for root directory
    struct inode root_inode;
    root_inode.ino = 0;
    root_inode.valid = 1;
    root_inode.size = 0;
    root_inode.type = S_IFDIR | 0755;
    root_inode.link = 2; // "." and ".." entries
    memset(root_inode.direct_ptr, -1, sizeof(root_inode.direct_ptr));
    memset(root_inode.indirect_ptr, -1, sizeof(root_inode.indirect_ptr));
    dir_add(root_inode, 0, ".", 1);
    dir_add(root_inode, 0, "..", 2);
    writei(0, &root_inode);

    free(i_bitmap);
    free(d_bitmap);

    return 0;
}


/* 
 * FUSE file operations
 */
static void *rufs_init(struct fuse_conn_info *conn) {
    log_rufs("--rufs_init--\n");

    if (access(diskfile_path, F_OK) == -1) {
        // Disk file not found, call mkfs to create a new file system
        rufs_mkfs();
    } else {
        // Disk file found, just initialize in-memory data structures
        // and read superblock from disk
        SB = (struct superblock *)malloc(sizeof(struct superblock));
        bio_read(0, SB);
    }

    return NULL;
}

static void rufs_destroy(void *userdata) {
    log_rufs("--rufs_destroy--\n");

    // Step 1: De-allocate in-memory data structures
    free(SB);

    // Step 2: Close diskfile
    dev_close();

}

static int rufs_getattr(const char *path, struct stat *stbuf) {
    log_rufs("--rufs_getattr--\n");
    printf("\t- path: \"%s\"\n", path);
    
    // Step 1: call get_node_by_path() to get inode from path
    struct inode inode;
    if(strcmp(path, "/") == 0) {
        readi(0, &inode);
    }
    else {
        if(get_node_by_path(path, 0, &inode) < 0) {
            debug("\t- File not found.\n");
            return -ENOENT;
        }
    }

    // Step 2: fill attribute of file into stbuf from inode
    stbuf->st_mode = inode.type;
    stbuf->st_nlink  = inode.link;
    stbuf->st_size  = inode.size;
    // stbuf->st_blksize = BLOCK_SIZE;
    time(&stbuf->st_atime);
    // time(&stbuf->st_mtime);

    printf("\t- mode: %d, links: %ld, size: %ld\n", stbuf->st_mode, stbuf->st_nlink, stbuf->st_size);

    return 0;
}

static int rufs_opendir(const char *path, struct fuse_file_info *fi) {
    log_rufs("--rufs_opendir--\n");
    printf("\t- path: \"%s\"\n", path);

    // Step 1: Call get_node_by_path() to get inode from path
    struct inode inode;
    if(strcmp(path, "/") == 0) {
        readi(0, &inode);
    }
    // Step 2: If not found, return -1
    else {
        if(get_node_by_path(path, 0, &inode) < 0) {
            return -1;
        }
    }

    return 0;
}

static int rufs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    log_rufs("--rufs_readdir--\n");
    printf("\t- path: \"%s\"\n", path);

    // Step 1: Call get_node_by_path() to get inode from path
    struct inode inode;
    if(strcmp(path, "/") == 0) {
        readi(0, &inode);
    }
    else {
        if(get_node_by_path(path, 0, &inode) < 0) {
            return -ENOENT;
        }
    }
    
    int current_offset = 0;
    // Step 2: Read directory entries from its data blocks, and copy them to filler
    for (int i = 0; i < 16; i++) {
        printf("\t- Check pointer %d.\n", i);

        if (inode.direct_ptr[i] != -1) {
            // Read a directory entries block
            struct dirent *entries = (struct dirent *)malloc(BLOCK_SIZE);
            bio_read(inode.direct_ptr[i], entries);
            debug("\t- Check dirent block.\n");
            
            // Step 3: Read directory's data block and check each directory entry.
            for (int j = 0; j < floor((BLOCK_SIZE / DIRENT_SIZE)); j++) {
                if (entries[j].valid) {
                    if (current_offset < offset) { //skip entries till offset is hit
                        current_offset += 1;
                        debug("\t- Skipping entry before offset.\n");
                        continue;
                    }
                    // copy dirents to fi with filler
                    struct stat st;
                    st.st_ino = entries[j].ino;
                    st.st_mode = S_IFDIR | 0755;

                    char* name = strdup(entries[j].name);

                    if(filler(buffer, name, &st, current_offset)) { //need to verify
                        return -ENOMEM;;
                    }

                }
            }
        }
    }
    if(current_offset == inode.size - 1) { //need to verify
        return 1;
    }
    return 0;
}


static int rufs_mkdir(const char *path, mode_t mode) {
    log_rufs("--rufs_mkdir--\n");

    // Step 1: Use dirname() and basename() to separate parent directory path and target directory name
    char *path_base_copy = strdup(path);
    char *base_name = basename(path_base_copy);

    struct inode *parent_inode = NULL;

    // Step 2: Call get_node_by_path() to get inode of parent directory
    get_node_by_path(path, 0, parent_inode);

    // Step 3: Call get_avail_ino() to get an available inode number
    int avail_ino = get_avail_ino();
    if(avail_ino == -1){
        debug("\t- Inode Not Available\n");
    }

    // Step 4: Call dir_add() to add directory entry of target directory to parent directory
    dir_add(*parent_inode, avail_ino, base_name, strlen(base_name));

    // Step 5: Update inode for target directory
    struct inode * new_inode = (struct inode *)malloc(sizeof(struct inode));

    new_inode->ino = avail_ino;
    new_inode->valid = 1;
    new_inode->size = 0;
    new_inode->type = S_IFDIR | 0755;
    new_inode->link = 2; // "." entry & ".." entry
    memset(new_inode->direct_ptr, -1, sizeof(new_inode->direct_ptr));
    memset(new_inode->indirect_ptr, -1, sizeof(new_inode->indirect_ptr));

    // Step 6: Call writei() to write inode to disk
    writei(avail_ino,new_inode);

    free(new_inode);    

    return 0;
}

static int rufs_rmdir(const char *path) {
    log_rufs("--rufs_rmdir--\n");

    // Step 1: Use dirname() and basename() to separate parent directory path and target directory name


    // Step 2: Call get_node_by_path() to get inode of target directory

    // Step 3: Clear data block bitmap of target directory

    // Step 4: Clear inode bitmap and its data block

    // Step 5: Call get_node_by_path() to get inode of parent directory

    // Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory

    return 0;
}

static int rufs_releasedir(const char *path, struct fuse_file_info *fi) {
    log_rufs("--rufs_releasedir--\n");
    printf("\t- path: \"%s\"\n", path);

    // For this project, you don't need to fill this function
    // But DO NOT DELETE IT!
    return 0;
}

static int rufs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    log_rufs("--rufs_create--\n");
    printf("\t- path: \"%s\"\n", path);

    // Step 1: Use dirname() and basename() to separate parent directory path and target file name

    // Step 2: Call get_node_by_path() to get inode of parent directory

    // Step 3: Call get_avail_ino() to get an available inode number

    // Step 4: Call dir_add() to add directory entry of target file to parent directory

    // Step 5: Update inode for target file

    // Step 6: Call writei() to write inode to disk

    return 0;
}

static int rufs_open(const char *path, struct fuse_file_info *fi) {
    log_rufs("--rufs_open--\n");
    printf("\t- path: \"%s\"\n", path);

    // Step 1: Call get_node_by_path() to get inode from path

    // Step 2: If not find, return -1

    return 0;
}

static int rufs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
    log_rufs("--rufs_read--\n");
    printf("\t- path: \"%s\"\n", path);

    // Step 1: You could call get_node_by_path() to get inode from path

    // Step 2: Based on size and offset, read its data blocks from disk

    // Step 3: copy the correct amount of data from offset to buffer

    // Note: this function should return the amount of bytes you copied to buffer
    return 0;
}

static int rufs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
    log_rufs("--rufs_write--\n");
    printf("\t- path: \"%s\"\n", path);
   // Step 1: You could call get_node_by_path() to get inode from path

    // Step 2: Based on size and offset, read its data blocks from disk

    // Step 3: Write the correct amount of data from offset to disk

    // Step 4: Update the inode info and write it to disk

    // Note: this function should return the amount of bytes you write to disk
    return size;
}

static int rufs_unlink(const char *path) {
    log_rufs("--rufs_unlink--\n");
    printf("\t- path: \"%s\"\n", path);

    // Step 1: Use dirname() and basename() to separate parent directory path and target file name

    // Step 2: Call get_node_by_path() to get inode of target file

    // Step 3: Clear data block bitmap of target file

    // Step 4: Clear inode bitmap and its data block

    // Step 5: Call get_node_by_path() to get inode of parent directory

    // Step 6: Call dir_remove() to remove directory entry of target file in its parent directory

    return 0;
}

static int rufs_truncate(const char *path, off_t size) {
    log_rufs("--rufs_truncate--\n");
    printf("\t- path: \"%s\"\n", path);
    // For this project, you don't need to fill this function
    // But DO NOT DELETE IT!
    return 0;
}

static int rufs_release(const char *path, struct fuse_file_info *fi) {
    log_rufs("--rufs_release--\n");
    printf("\t- path: \"%s\"\n", path);
    // For this project, you don't need to fill this function
    // But DO NOT DELETE IT!
    return 0;
}

static int rufs_flush(const char * path, struct fuse_file_info * fi) {
    log_rufs("--rufs_flush--\n");
    printf("\t- path: \"%s\"\n", path);
    // For this project, you don't need to fill this function
    // But DO NOT DELETE IT!
    return 0;
}

static int rufs_utimens(const char *path, const struct timespec tv[2]) {
    log_rufs("--rufs_utimens--\n");
    printf("\t- path: \"%s\"\n", path);
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



// Print helpers
// Display implementations

#define DEBUG 1
#define RUFS_LOG 1

#define BLACK 		0
#define RED 		1
#define YELLOW		3
#define BLUE		4
#define MAGENTA		5
#define	WHITE		7

#define CYAN		87
#define LIME        82
#define ORANGE      202
#define PURPLE      93

#define FAKE_RESET  7

void text_color(int fg) {
	printf("%c[38;5;%dm", 0x1B, fg);
}
void text_color_bg(int fg, int bg) {
	printf("%c[38;5;%d;48;5;%dm", 0x1B, fg, bg);
}

void reset_color() {
    printf("\033[0m");
}

// print debug messages in ORANGE toggle with DEBUG
void debug(char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    
    if(DEBUG) {
        text_color(ORANGE);
        printf(fmt, args);
        reset_color();
    }
}

/* print log for rufs operations only in PURPLE
 * toggle with RUFS_LOG
 */
void log_rufs(char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    if(RUFS_LOG) {
        text_color(PURPLE);
        printf(fmt, args);
        reset_color();
    }
}