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
void print_bitmap(char* name, bitmap_t bitmap, int num_bytes);
int blocks_used = 0;


void show_bitmaps() {
    bitmap_t i_bitmap = (bitmap_t)malloc(BLOCK_SIZE);
    bio_read(SB->i_bitmap_blk, i_bitmap);

    bitmap_t d_bitmap = (bitmap_t)malloc(BLOCK_SIZE);
    bio_read(SB->d_bitmap_blk, d_bitmap);

    print_bitmap("\t- Inodes", i_bitmap, 8);
    print_bitmap("\t- Data", d_bitmap, 8);
}

/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {
    debug("\t-> get_avail_ino\n");
    // Step 1: Read inode bitmap from disk
    bitmap_t i_bitmap = (bitmap_t)malloc(BLOCK_SIZE);
    bio_read(SB->i_bitmap_blk, i_bitmap);
    
    print_bitmap("\t- Inodes", i_bitmap, 6);
    
    int inode_idx;
    // Step 2: Traverse inode bitmap to find an available slot
    for (inode_idx = 0; inode_idx < SB->max_inum; inode_idx++)
    {
        if(get_bitmap( i_bitmap, inode_idx)==0)
        {
            // Step 3: Update inode bitmap and write to disk
            printf("\t- Update inode bitmap at %d\n", inode_idx);
            set_bitmap( i_bitmap, inode_idx);
            bio_write(SB->i_bitmap_blk, i_bitmap);
            free(i_bitmap);
            printf("\t\t- Available inode: %d.\n", inode_idx);
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
    debug("\t-> get_avail_blkno\n");

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
            blocks_used++;
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
    int offset = (ino % INODES_PER_BLOCK);
    printf("\t- readi. ino: %d, block_num: %d, offset: %d\n", ino, block_num, offset);
    struct inode * inode_block = (struct inode *) malloc(BLOCK_SIZE);

    // Step 3: Read the block from disk and then copy into inode structure
    bio_read(block_num, inode_block);
    memcpy(inode, &inode_block[ino % INODES_PER_BLOCK] , INODE_SIZE);
    // memcpy(inode, inode_block + offset, INODE_SIZE);
	free(inode_block);

    return 0;
}

int writei(uint16_t ino, struct inode *inode) {

    // Step 1: Get the block number where this inode resides on disk
    int block_num =  SB->i_start_blk + ((ino * INODE_SIZE ) / BLOCK_SIZE);

    // Step 2: Get the offset in the block where this inode resides on disk
    int offset = (ino % INODES_PER_BLOCK);
    printf("\t- writei. ino: %d, block_num: %d, offset: %d\n", ino, block_num, offset);
    struct inode * inode_block = (struct inode *) malloc(BLOCK_SIZE);

    int ret_value = 0;
    // Step 3: Write inode to disk
    ret_value = bio_read(block_num, inode_block);
    if(ret_value < 0) {
        debug("\t- bio_read failed in writei.\n");
        return ret_value;
    }
    debug("\t- Read old inode block.\n");
    memcpy(&inode_block[ino % INODES_PER_BLOCK], inode , INODE_SIZE);
    // memcpy(inode_block + offset, inode , INODE_SIZE);

    ret_value = bio_write(block_num, inode_block);
    if(ret_value < 0) {
        debug("\t- bio_write failed in writei.\n");
        return ret_value;
    }
    debug("\t- Wrote new inode to block.\n");

	free(inode_block);
    
    return 0;
}


/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {
    debug("\t-> dir_find\n");
    printf("\t- ino: %d, fname: %s, name_len: %ld\n", ino, fname, name_len);
    // Step 1: Call readi() to get the inode using ino (inode number of current directory)
    struct inode dir_inode;
    readi(ino, &dir_inode);
    
    printf("\t- dir_inode: ino: %d, direct_ptr[]: ", dir_inode.ino);
    for (int i = 0; i < 16; i++) { //verification of pointers
        printf(" %d", dir_inode.direct_ptr[i]);
    }
    printf("\n");
    
    int ret_value;
    // Step 2: Get data block of current directory from inode
    for (int i = 0; i < 16; i++) {
        if (dir_inode.direct_ptr[i] != -1) {
            printf("\t\t- Valid ptr found: %d", dir_inode.direct_ptr[i]);
            
            // Read a directory entries block
            struct dirent *entries = (struct dirent *)malloc(BLOCK_SIZE);
            ret_value = bio_read(dir_inode.direct_ptr[i], entries);

            
            printf("\t\t- bio_read return: %d\n", ret_value);
            if( ret_value < 0) {
                debug("\t- bio_read failed in dir_find.\n");
                return ret_value;
            }
            
            
            // Step 3: Read directory's data block and check each directory entry.
            for (int j = 0; j < floor((BLOCK_SIZE / DIRENT_SIZE)); j++) {
                if(entries[j].valid) {
                    printf("\t- entries[%d]: valid: %d", j, entries[j].valid);
                    printf(", name: %s\n", entries[j].name);
                }
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

    return -ENOENT;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {
    debug("\t-> dir_add\n");
    printf("\t- parent ino: %d, f_ino: %d, fname: %s, name_len: %ld\n", dir_inode.ino, f_ino, fname, name_len);
    show_bitmaps();
    int ret_value;
    // Step 1: Read dir_inode's data block and check each directory entry of dir_inode
    
    struct dirent *find_dirent = (struct dirent*) malloc(sizeof(struct dirent));
    ret_value = dir_find(dir_inode.ino, fname, name_len, find_dirent);
    if(ret_value == 0) {
        debug("\t- Entry already exists.\n");
        return -EEXIST;
    }
    else if(ret_value < 0 && ret_value != -ENOENT) {
        printf("Error searching for entry.\n");
        return ret_value;
    }
    
    debug("\t- File doesn't already exist.\n");
    printf("\t- dir_inode: ino: %d, direct_ptr[]: ", dir_inode.ino);
    for (int i = 0; i < 16; i++) { //verification of pointers
        printf(" %d", dir_inode.direct_ptr[i]);
    }
    printf("\n");
    int foundSlot = 0;
    // Step 3: Add directory entry in dir_inode's data block and write to disk
    for (int i = 0; i < 16; i++) {
        // check all existing pointers for a block with an empty slot
        if (dir_inode.direct_ptr[i] != -1) {
            struct dirent *entries = (struct dirent *)malloc(BLOCK_SIZE);
            ret_value = bio_read(dir_inode.direct_ptr[i], entries);
            if(ret_value < 0) {
                printf("bio_read failed in dir_add\n");
                return ret_value;
            }

            for (int j = 0; j < floor(BLOCK_SIZE / DIRENT_SIZE); j++) {
                if (!entries[j].valid) {
                    foundSlot = 1;

                    debug("\t- Found slot for new entry in existing block.\n");
                    entries[j].valid = 1;
                    entries[j].ino = f_ino;
                    strcpy(entries[j].name, fname);
                    bio_write(dir_inode.direct_ptr[i], entries);
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
                debug("\t- Made new block to store dirents.\n");

                int block_num = get_avail_blkno();
                if(block_num < 0) {
                    return -EMLINK;
                }
                block_num += SB->d_start_blk;
                dir_inode.direct_ptr[i] = block_num;

                ret_value = writei(dir_inode.ino, &dir_inode);
                if (ret_value < 0 ) {
                    printf("writei failed in dir_add for dir_inode.\n");
                    return ret_value;
                }

                struct dirent *entries = (struct dirent *)malloc(BLOCK_SIZE);
                memset(entries, 0, BLOCK_SIZE);
                entries[0].valid = 1;
                entries[0].ino = f_ino;
                strcpy(entries[0].name, fname);
                bio_write(block_num, entries);
                break;
            }
        }
    }

    if(!foundSlot) {
        printf("All dirent slots full in this directory.");
        return -1; //errorno?
    }
    // Update directory inode
    dir_inode.size += 1;
    time(&dir_inode.vstat.st_atime);
    time(&dir_inode.vstat.st_mtime);

    // Write directory entry
    printf("\t- Printing bitmaps in dir_add\n");
    show_bitmaps();
    writei(dir_inode.ino, &dir_inode);
    debug("\t- Added directory entry.\n");
    show_bitmaps();
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
    debug("\t-> get_node_by_path\n");
    printf("\t\t- path: %s, parent ino: %d\n", path, ino);
    // Step 1: Resolve the path name, walk through path, and finally, find its inode.
    // Note: You could either implement it in a iterative way or recursive way
    char *token, *rest = NULL;
    char *path_copy = strdup(path);

    struct dirent dirent;
    // /abc/xzy/two.txt
    // /one.txt
    while ((token = strtok_r(rest ? rest : path_copy, "/", &rest)) != NULL) {
        printf("\t\t- Token: %s, rest: %s\n", token, rest);

        // Find the directory entry in the current directory
        if (dir_find(ino, token, strlen(token), &dirent) != 0) {
            free(path_copy);
            printf("\t\t- Entry not found for token.\n");
            return -1; // Directory entry not found
        }

        // Update ino for the next iteration
        ino = dirent.ino;
    }

    printf("\t\t- inode: %d\n", ino);
    // Copy the final inode to the provided inode structure
    // Read the inode of the found entry
    if (readi(ino, inode) != 0) {
        printf("\t\t- Failed to read inode.\n");
        return -1; // Failed to read inode
    }
    
    free(path_copy);
    debug("\t- get_node_by_path Successful Return\n");
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
    printf("\t- SB->i_start_blk: %d, SB->d_start_blk: %d\n", SB->i_start_blk, SB->d_start_blk);
    // write superblock to disk
        
        debug("\t- Write Superblock to disk.\n");
    bio_write(0, SB);

    // initialize inode bitmap
    bitmap_t i_bitmap = (bitmap_t)malloc(BLOCK_SIZE);
    memset(i_bitmap, 0, BLOCK_SIZE);

    // initialize data block bitmap
    bitmap_t d_bitmap = (bitmap_t)malloc(BLOCK_SIZE);
    memset(d_bitmap, 0, BLOCK_SIZE);

    // update bitmap information for root directory
    set_bitmap(i_bitmap, 0); // Root directory's inode is in use
    // set_bitmap(d_bitmap, 0); // Root directory's data block is in use

        debug("\t- Write both bitmaps to disk.\n");
    bio_write(SB->i_bitmap_blk, i_bitmap);
    bio_write(SB->d_bitmap_blk, d_bitmap);
    
    show_bitmaps();

    // update inode for root directory
    struct inode root_inode;
    root_inode.ino = 0;
    root_inode.valid = 1;
    root_inode.size = 0;
    root_inode.type = 0;
    root_inode.link = 2; // "." and ".." entries
    // memset(root_inode.direct_ptr, -1, sizeof(root_inode.direct_ptr));
    // memset(root_inode.indirect_ptr, -1, sizeof(root_inode.indirect_ptr));
    for(int i = 0; i < 16; i++) {
        root_inode.direct_ptr[i] = -1;
    }

    memset(&root_inode.vstat, 0, sizeof(struct stat));
    root_inode.vstat.st_mode = S_IFDIR | 0755; 
	root_inode.vstat.st_nlink = root_inode.link; 
	root_inode.vstat.st_atime = time(NULL); 
	root_inode.vstat.st_mtime = time(NULL);

    writei(0, &root_inode);

    dir_add(root_inode, 0, ".", 1);

    readi(0, &root_inode);
    dir_add(root_inode, 0, "..", 2);
    // writei(0, &root_inode);

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
    stbuf->st_mode = inode.type ? S_IFREG : S_IFDIR | 0755;
    stbuf->st_nlink  = inode.link;
    stbuf->st_size  = inode.vstat.st_size;
    // stbuf->st_blksize = BLOCK_SIZE;
    time(&stbuf->st_atime);
    // time(&stbuf->st_mtime);

    printf("\t- mode: %d, links: %ld, size: %ld\n", stbuf->st_mode, stbuf->st_nlink, stbuf->st_size);
    printf(" --------------------------------- BLOCKS USED : %d ---------------------------------\n",blocks_used);
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
            return -ENOENT;
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
                        return -ENOMEM; //verify
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
    pthread_mutex_lock(&lock);

    // Step 1: Use dirname() and basename() to separate parent directory path and target directory name
    char *path_dir_copy = strdup(path);
    char *path_base_copy = strdup(path);
    char *dir_name = dirname(path_dir_copy);
    char *base_name = basename(path_base_copy);

    printf("\t- Parent Path : %s\n",dir_name);
    printf("\t- Target Path : %s\n",base_name);

    struct inode * parent_inode = (struct inode *)malloc(sizeof(struct inode));

    // Step 2: Call get_node_by_path() to get inode of parent directory
    int ret_value = get_node_by_path(dir_name, 0, parent_inode);

    if( ret_value < 0) {
        debug("\t- Get_node_by_path Failed in rufs_mkdir \n");
        pthread_mutex_unlock(&lock);
        return ret_value;
    }
    printf("\t- Parent inode: ino: %d, size: %d\n", parent_inode->ino, parent_inode->size);

    // Step 3: Call get_avail_ino() to get an available inode number
    int avail_ino = get_avail_ino();
    if(avail_ino == -1){
        debug("\t- Inode Not Available\n");
    }
    printf("\t- Avail_ino %d in rufs_mkdir\n",avail_ino);
    debug("\t- Add entry to directory.\n");
    // Step 4: Call dir_add() to add directory entry of target directory to parent directory
    ret_value = dir_add(*parent_inode, avail_ino, base_name, strlen(base_name));

    if( ret_value < 0) {
        debug("\t- dir_add Failed in rufs_mkdir \n");
        pthread_mutex_unlock(&lock);
        return ret_value;
    }

    // Step 5: Update inode for target directory
    struct inode * new_inode = (struct inode *)malloc(sizeof(struct inode));

    new_inode->ino = avail_ino;
    new_inode->valid = 1;
    new_inode->size = 0;
    new_inode->type = 0; //directory
    new_inode->link = 2; // "." entry & ".." entry
    // memset(new_inode->direct_ptr, -1, sizeof(new_inode->direct_ptr));
    // memset(new_inode->indirect_ptr, -1, sizeof(new_inode->indirect_ptr));
    for(int i = 0; i < 16; i++) {
        new_inode->direct_ptr[i] = -1;
    }
    memset(&new_inode->vstat, 0, sizeof(struct stat));
    new_inode->vstat.st_mode = S_IFDIR | 0755; 
	new_inode->vstat.st_nlink = new_inode->link; 
	new_inode->vstat.st_atime = time(NULL); 
	new_inode->vstat.st_mtime = time(NULL);

    // Step 6: Call writei() to write inode to disk
    debug("\t- Writing new inode to disk.\n");
    ret_value = writei(avail_ino, new_inode);
    if(ret_value < 0) {
        debug("\t- writei failed in mkdir.\n");
        pthread_mutex_unlock(&lock);
        return ret_value;
    }

    dir_add(*new_inode, new_inode->ino, ".", 1);

    readi(avail_ino, new_inode);
    dir_add(*new_inode, parent_inode->ino, "..", 2);

    free(new_inode);
    free(parent_inode);    
    pthread_mutex_unlock(&lock);
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
    printf(" --------------------------------- BLOCKS USED : %d ---------------------------------\n",blocks_used);

    // For this project, you don't need to fill this function
    // But DO NOT DELETE IT!
    return 0;
}

static int rufs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    log_rufs("--rufs_create--\n");
    pthread_mutex_lock(&lock);
    printf("\t- path: \"%s\"\n", path);

    // Step 1: Use dirname() and basename() to separate parent directory path and target file name
    char *path_dir_copy = strdup(path);
    char *path_base_copy = strdup(path);
    char *dir_name = dirname(path_dir_copy);
    char *base_name = basename(path_base_copy);

    // struct inode *parent_inode = NULL;

    // Step 2: Call get_node_by_path() to get inode of parent directory
    struct inode *parent_inode = (struct inode *)malloc(sizeof(struct inode));

    int ret_value = get_node_by_path(dir_name, 0, parent_inode);

    if( ret_value < 0) {
        debug("\t- Get_node_by_path Failed\n");
        pthread_mutex_unlock(&lock);
        return ret_value;
    }


    // Step 3: Call get_avail_ino() to get an available inode number
    int avail_ino = get_avail_ino();
    if(avail_ino < 0){
        debug("\t- get_avail_ino Failed\n");
        pthread_mutex_unlock(&lock);
        return ret_value;
    }

    // Step 4: Call dir_add() to add directory entry of target file to parent directory
    ret_value = dir_add(*parent_inode, avail_ino, base_name, strlen(base_name));

    if( ret_value < 0) {
        debug("\t- Dir_add Failed\n");
        pthread_mutex_unlock(&lock);
        return ret_value;
    }

    // Step 5: Update inode for target file
    struct inode * target_inode = (struct inode *)malloc(sizeof(struct inode));

    target_inode->ino = avail_ino;
    target_inode->valid = 1;
    target_inode->size = 0;
    target_inode->type = S_IFREG;
    target_inode->link = 2; // "." entry & ".." entry
    // memset(target_inode->direct_ptr, -1, sizeof(target_inode->direct_ptr));
    // memset(target_inode->indirect_ptr, -1, sizeof(target_inode->indirect_ptr));
    for(int i = 0; i < 16; i++) {
        target_inode->direct_ptr[i] = -1;
    }

    memset(&target_inode->vstat, 0, sizeof(struct stat));
    target_inode->vstat.st_mode = S_IFREG ;
	target_inode->vstat.st_nlink = target_inode->link; 
	target_inode->vstat.st_atime = time(NULL); 
	target_inode->vstat.st_mtime = time(NULL);

    // Step 6: Call writei() to write inode to disk
    writei(avail_ino, target_inode);
    pthread_mutex_unlock(&lock);
    return 0;
}

static int rufs_open(const char *path, struct fuse_file_info *fi) {
    log_rufs("--rufs_open--\n");
    printf("\t- path: \"%s\"\n", path);

    char *file_path = strdup(path);

    // Step 1: Call get_node_by_path() to get inode from path
    struct inode *file_inode = (struct inode *)malloc(sizeof(struct inode));

    int ret_value = get_node_by_path(file_path, 0, file_inode);

    // Step 2: If not find, return -1
    if( ret_value < 0) {
        debug("\t- Get_node_by_path Failed in rufs open\n");
        return -1;
    }
    
    free(file_inode);
    return 0;
}

static int rufs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
    log_rufs("--rufs_read--\n");
    printf("\t- path: \"%s\"\n", path);
    
    int read_size = 0;

    struct inode *file_inode = (struct inode *)malloc(sizeof(struct inode));

    char *buffer_slider = buffer;

    char *data_block = (char *)malloc(BLOCK_SIZE);

    // Step 1: You could call get_node_by_path() to get inode from path
    int ret_value = get_node_by_path(path, 0, file_inode);    
    
    if(ret_value < 0) {
        debug("\t- Get_node_by_path failed in rufs_write\n");
        return ret_value;
        // return -ENOENT;
    }

    // Step 2: Based on size and offset, read its data blocks from disk
    int remaining_bytes = size;
    int remaining_offset = offset;
    while(remaining_bytes > 0){

        int block_idx = remaining_offset / BLOCK_SIZE ;
        int remaining_block = BLOCK_SIZE - (remaining_offset % BLOCK_SIZE);

        if(block_idx > 15){
            debug("\t- Size Exceeded \n");
            return -1;
        }

        if(remaining_bytes <= remaining_block){
            remaining_bytes = 0;
            remaining_offset += remaining_block;
        }
        else{
            remaining_bytes -= remaining_block;
            remaining_offset += remaining_block;
        }
    }

    remaining_bytes = size;
    remaining_offset = offset;
    
    while(remaining_bytes > 0){
        
        int block_idx = remaining_offset / BLOCK_SIZE ;
        int remaining_block = BLOCK_SIZE - (remaining_offset % BLOCK_SIZE);

        if(file_inode->direct_ptr[block_idx] == -1){
            debug("\t- Direct Pointer is Invalid\n");
            return -1;
        }

        if(remaining_bytes <= remaining_block){

            ret_value = bio_read(file_inode->direct_ptr[block_idx],data_block);

            if(ret_value < 0) {
                debug("\t- bio_read failed in rufs_write.\n");
                return ret_value;
            }

            memcpy(buffer_slider, data_block+ (BLOCK_SIZE - remaining_block), remaining_bytes);
            
            buffer_slider += remaining_bytes;
            read_size += remaining_bytes;
            remaining_bytes = 0;
            remaining_offset += remaining_block;
        }
        else{

            ret_value = bio_read(file_inode->direct_ptr[block_idx],data_block);

            if(ret_value < 0) {
                debug("\t- bio_read failed in rufs_write.\n");
                return ret_value;
            }

            memcpy(buffer_slider, data_block + (BLOCK_SIZE - remaining_block), remaining_block);
            
            buffer_slider += remaining_block;
            read_size += remaining_block;
            remaining_bytes -= remaining_block;
            remaining_offset += remaining_block;
        }

    }

    // Step 3: copy the correct amount of data from offset to buffer
    time(&(file_inode->vstat.st_atime));

    writei(file_inode->ino,file_inode);

    // Note: this function should return the amount of bytes you copied to buffer
    free(data_block);
    free(file_inode);
    printf("\t- size : %ld and output size : %d\n",size,read_size);
    return read_size;
}

static int rufs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
    log_rufs("--rufs_write--\n");
    pthread_mutex_lock(&lock);
    printf("\t- path: \"%s\"\n", path);
    
    int write_size = 0;

    struct inode *file_inode = (struct inode *)malloc(sizeof(struct inode));

    //TODO
    const char *buffer_slider = buffer;

    char *data_block = (char *)malloc(BLOCK_SIZE);
    
    // Step 1: You could call get_node_by_path() to get inode from path
    int ret_value = get_node_by_path(path, 0, file_inode);    
    
    if(ret_value < 0) {
        debug("\t- Get_node_by_path failed in rufs_write\n");
        pthread_mutex_unlock(&lock);
        return ret_value;
        // return -ENOENT;
    }

    // Step 2: Based on size and offset, read its data blocks from disk

    int remaining_bytes = size;
    int remaining_offset = offset;
    while(remaining_bytes > 0){

        int block_idx = remaining_offset / BLOCK_SIZE ;
        int remaining_block = BLOCK_SIZE - (remaining_offset % BLOCK_SIZE);

        if(block_idx > 15){
            debug("\t- Size Exceeded \n");
            pthread_mutex_unlock(&lock);
            return -1;
        }

        if(remaining_bytes <= remaining_block){
            remaining_bytes = 0;
            remaining_offset += remaining_block;
        }
        else{
            remaining_bytes -= remaining_block;
            remaining_offset += remaining_block;
        }
    }

    remaining_bytes = size;
    remaining_offset = offset;
    
    while(remaining_bytes > 0){
        
        int block_idx = remaining_offset / BLOCK_SIZE ;
        int remaining_block = BLOCK_SIZE - (remaining_offset % BLOCK_SIZE);

        if(file_inode->direct_ptr[block_idx] == -1){
            int block_num = get_avail_blkno();
            if(block_num < 0) {
                pthread_mutex_unlock(&lock);
                return -EMLINK;
            }
            block_num += SB->d_start_blk;
            file_inode->direct_ptr[block_idx] = block_num; // time? 
            //TODO
            writei(file_inode->ino, file_inode);
        }

        if(remaining_bytes <= remaining_block){
            
            ret_value = bio_read(file_inode->direct_ptr[block_idx],data_block);

            if(ret_value < 0) {
                debug("\t- bio_read failed in rufs_write.\n");
                pthread_mutex_unlock(&lock);
                return ret_value;
            }

            memcpy(data_block+ (BLOCK_SIZE - remaining_block), buffer_slider, remaining_bytes);

            ret_value = bio_write(file_inode->direct_ptr[block_idx],data_block);

            if(ret_value < 0) {
                debug("\t- bio_write failed in rufs_write.\n");
                pthread_mutex_unlock(&lock);
                return ret_value;
            }

            
            buffer_slider += remaining_bytes;
            write_size += remaining_bytes;
            remaining_bytes = 0;
            remaining_offset += remaining_block;
        }
        else{

            ret_value = bio_read(file_inode->direct_ptr[block_idx],data_block);

            if(ret_value < 0) {
                debug("\t- bio_read failed in rufs_write.\n");
                pthread_mutex_unlock(&lock);
                return ret_value;
            }

            memcpy(data_block + (BLOCK_SIZE - remaining_block), buffer_slider, remaining_block);

            ret_value = bio_write(file_inode->direct_ptr[block_idx],data_block);

            if(ret_value < 0) {
                debug("\t- bio_write failed in rufs_write.\n");
                pthread_mutex_unlock(&lock);
                return ret_value;
            }
            
            buffer_slider += remaining_block;
            write_size += remaining_block;
            remaining_bytes -= remaining_block;
            remaining_offset += remaining_block;
        }

    }

    // Step 3: Write the correct amount of data from offset to disk

    // Step 4: Update the inode info and write it to disk
    file_inode->vstat.st_size += size;
    printf("\t-------------------------------------------------------------------------\n");
    printf("\t- size : %ld and vstat.st_size : %ld\n",size,file_inode->vstat.st_size);
    time(&(file_inode->vstat.st_atime));
    time(&(file_inode->vstat.st_mtime));

    writei(file_inode->ino,file_inode);

    // Note: this function should return the amount of bytes you write to disk
    free(data_block);
    free(file_inode);
    // printf("\t- size : %ld and output size : %d\n",size,write_size);
    pthread_mutex_unlock(&lock);
    return write_size;
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

const char *bit_rep[16] = {
    [ 0] = "0000", [ 1] = "0001", [ 2] = "0010", [ 3] = "0011",
    [ 4] = "0100", [ 5] = "0101", [ 6] = "0110", [ 7] = "0111",
    [ 8] = "1000", [ 9] = "1001", [10] = "1010", [11] = "1011",
    [12] = "1100", [13] = "1101", [14] = "1110", [15] = "1111",
};

#define BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)  \
  ((byte) & 0x80 ? '1' : '0'), \
  ((byte) & 0x40 ? '1' : '0'), \
  ((byte) & 0x20 ? '1' : '0'), \
  ((byte) & 0x10 ? '1' : '0'), \
  ((byte) & 0x08 ? '1' : '0'), \
  ((byte) & 0x04 ? '1' : '0'), \
  ((byte) & 0x02 ? '1' : '0'), \
  ((byte) & 0x01 ? '1' : '0') 

void print_byte(char byte)
{
    printf(BINARY_PATTERN, BYTE_TO_BINARY(byte));
}


void print_bitmap(char* name, bitmap_t bitmap, int num_bytes) {
    printf("\t%s: ", name);

    text_color_bg(BLACK, YELLOW);
    for(int i = num_bytes-1; i >= 0; i--) {
        print_byte(bitmap[i]);
        if(i != 0)
            printf("  ");
    }
    reset_color();
    printf("\n");
}