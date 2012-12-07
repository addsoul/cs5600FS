/*
 * file:        homework.c
 * description: skeleton file for CS 5600 homework 3
 *
 * CS 5600, Computer Systems, Northeastern CCIS
 * Peter Desnoyers, updated April 2012
 * $Id: homework.c 452 2011-11-28 22:25:31Z pjd $
 */

#define FUSE_USE_VERSION 27
#define DIR_BLOCK_SIZE 64
#define FILENAME_MAX_LENGTH 43
#define FILENAME_OFFSET 20
#define MAX_DIR 16
#define CEILING(X) (X-(int)(X) > 0 ? (int)(X+1) : (int)(X))

#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <fuse.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "cs5600fs.h"
#include "blkdev.h"

/* 
 * disk access - the global variable 'disk' points to a blkdev
 * structure which has been initialized to access the image file.
 *
 * NOTE - blkdev access is in terms of 512-byte SECTORS, while the
 * file system uses 1024-byte BLOCKS. Remember to multiply everything
 * by 2.
 */

extern struct blkdev *disk;

struct cs5600fs_super superblock;

struct cs5600fs_entry* cs5600fs_fat;

char *fat = NULL;

struct cs5600fs_dirent dirent;
struct cs5600fs_dirent directory[16];

char* strwrd(char*, char*, size_t, char*);
int getBlockIndexFor(char*, char*, int*);
void copyDirToDirent(struct cs5600fs_dirent);
void readFAT(void);
void writeFAT(void);
int getFreeFAT(void);
int hw3_create_helper(char *, mode_t, struct fuse_file_info*, char);
int in_same_dir(char *, char *);

/* init - this is called once by the FUSE framework at startup.
 * This might be a good place to read in the super-block and set up
 * any global variables you need. You don't need to worry about the
 * argument or the return value.
 */
void* hw3_init(struct fuse_conn_info *conn) {
    char block[SECTOR_SIZE * 2];
    disk->ops->read(disk, 0, 2, block);
    superblock.magic = *((unsigned int *) block);
    superblock.blk_size = *((unsigned int *) (block + 4)); // Always 1024
    superblock.fs_size = *((unsigned int *) (block + 8)); // 1024-byte blocks
    superblock.fat_len = *((unsigned int *) (block + 12)); // 1024-byte blocks
    superblock.root_dirent = *((struct cs5600fs_dirent *) (block + 16));
    
    readFAT();
    return NULL;
}

void readFAT() {
    
    fat = (char *) malloc(SECTOR_SIZE * 2 * superblock.fat_len);
    
    // Read FAT from superblock
    int i = 0;
    for (i = 0; i < superblock.fat_len; i++) {
        disk->ops->read(disk, 2 + (i * 2), 2, fat + (i * SECTOR_SIZE * 2));
    }
    
    cs5600fs_fat = (struct cs5600fs_entry*) fat;
}

void writeFAT() {
    int i;
    for (i = 0; i < superblock.fat_len; i++) {
        disk->ops->write(disk, 2 + i * 2, 2, fat + i * SECTOR_SIZE * 2);
    }
}

/**
 * Get access to the block of directory for specified file/directory
 * eg. /root/home/xyz/abc.txt will result in block corresponding to the
 * /root/home/xyz/ directory
 * @param: path path of the file / directory
 * @param: block block that stores the block directory
 * @param: blkPos the block number of the directory in the image
 *
 * Return the blkPos of the directory corresponding to the given file
 **/

int getBlockIndexFor(char* path, char* block, int* blkPos) {
    char pathFields[50][FILENAME_MAX_LENGTH + 1];
    int fieldCount;
    //struct cs5600fs_dirent directory[16];
    char* line = (char *) path;
    for (fieldCount = 0; fieldCount < 50; fieldCount++) {
        line = strwrd(line, pathFields[fieldCount], FILENAME_MAX_LENGTH + 1, "/");
        if (line == NULL) {
            break;
        }
    }
    int i, j;
    int dirStart = superblock.root_dirent.start;
    int indexStart = 0;

    for (i = 0; i <= fieldCount; i++) {
        // Read Dir data into block jq
        disk->ops->read(disk, dirStart * 2, 2, (void*) directory);

        for (j = 0; j < MAX_DIR; j++) {
            // If directory or filename is found on the disk
            if (strcmp(pathFields[i], directory[j].name) == 0) {
                // If directory then stop searching for next name
                if (directory[j].isDir != 1 && i<fieldCount){
                    return -ENOTDIR;
                }
                indexStart = dirStart;
                dirStart = directory[j].start;
                memcpy(&dirent, &directory[j], sizeof (dirent));
		break;
            }
        }
        if (j == MAX_DIR) {
            return -ENOENT;
        }
    }
    *blkPos = indexStart;
    return j;
}

/* find the next word starting at 's', delimited by characters
 * in the string 'delim', and store up to 'len' bytes into *buf
 * returns pointer to immediately after the word, or NULL if done.
 */
char *strwrd(char *s, char *buf, size_t len, char *delim) {
    s += strspn(s, delim);
    int n = strcspn(s, delim); /* count the span (spn) of bytes in */
    if (len - 1 < n) /* the complement (c) of *delim */
        n = len - 1;
    memcpy(buf, s, n);
    buf[n] = 0;
    s += n;
    return (*s == 0) ? NULL : s;
}
/* Note on path translation errors:
 * In addition to the method-specific errors listed below, almost
 * every method can return one of the following errors if it fails to
 * locate a file or directory corresponding to a specified path.
 *
 * ENOENT - a component of the path is not present.
 * ENOTDIR - an intermediate component of the path (e.g. 'b' in
 *           /a/b/c) is not a directory
 */

/* getattr - get file or directory attributes. For a description of
 *  the fields in 'struct stat', see 'man lstat'.
 *
 * Note - fields not provided in CS5600fs are:
 *    st_nlink - always set to 1
 *    st_atime, st_ctime - set to same value as st_mtime
 *
 * errors - path translation, ENOENT
 */
static int hw3_getattr(const char *path, struct stat *sb) {
    char block[SECTOR_SIZE * 2];//, block1[SECTOR_SIZE * 2];
    int blkPos;

    sb->st_dev = 0;
    sb->st_ino = 0;
    sb->st_rdev = 0;

    if (strcmp(path, "/") == 0) {
	disk->ops->read(disk, superblock.root_dirent.start * 2, 2, block);
	sb->st_uid = superblock.root_dirent.uid;
        sb->st_gid = superblock.root_dirent.gid;
        sb->st_mode = superblock.root_dirent.mode | S_IFDIR;
        sb->st_nlink = 1;
        sb->st_size = 0;
        sb->st_atime = superblock.root_dirent.mtime;
        sb->st_mtime = superblock.root_dirent.mtime;
        sb->st_ctime = superblock.root_dirent.mtime;
        sb->st_blocks = 0;
	sb->st_blksize = SECTOR_SIZE *2;
        return 0;
    }
    int posInDir = getBlockIndexFor((char *) path, block, &blkPos);
    if (posInDir < 0) return posInDir;
    sb->st_uid = dirent.uid;
    sb->st_gid = dirent.gid;
    sb->st_mode = dirent.mode | (dirent.isDir ? S_IFDIR : S_IFREG);
    sb->st_nlink = 1;
    sb->st_size = dirent.length;
    sb->st_atime = dirent.mtime;
    sb->st_mtime = dirent.mtime;
    sb->st_ctime = dirent.mtime;
    sb->st_blocks = (sb->st_size + SECTOR_SIZE * 2 - 1) / (SECTOR_SIZE * 2);
    sb->st_blksize = SECTOR_SIZE * 2;
    sb->st_size = dirent.isDir ? 0 : dirent.length;
    return 0;

    //}

    //return -EOPNOTSUPP;
}

/* readdir - get directory contents.
 *
 * for each entry in the directory, invoke the 'filler' function,
 * which is passed as a function pointer, as follows:
 *     filler(buf, <name>, <statbuf>, 0)
 * where <statbuf> is a struct stat, just like in getattr.
 *
 * Errors - path resolution, ENOTDIR, ENOENT
 */
static int hw3_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
        off_t offset, struct fuse_file_info *fi) {
    char *name = NULL;
    struct stat sb;
    char block[SECTOR_SIZE * 2];
    int start = 0, posInDir = 0, blkPos = 0;

    if (strcmp("/", path) == 0) {
        start = superblock.root_dirent.start;
    } else {
        posInDir = getBlockIndexFor((char*) path, block, &blkPos);

        if (posInDir < 0) return posInDir;
        start = dirent.start;
        if (dirent.isDir != 1) {
            return -ENOTDIR;
        }
    }

    disk->ops->read(disk, start * 2, 2, (void *) directory);
    memset(&sb, 0, sizeof (sb));
    int i = 0;
    for (i = 0; i < MAX_DIR; i++) {
        if (directory[i].valid == 1) {
	  name = directory[i].name;
	  sb.st_uid = directory[i].uid;
	  sb.st_gid = directory[i].gid;
	  sb.st_mode = directory[i].mode | (directory[i].isDir ? S_IFDIR : S_IFREG);
	  sb.st_atime = directory[i].mtime;
	  sb.st_mtime = directory[i].mtime;
	  sb.st_ctime = directory[i].mtime;
	  sb.st_nlink = 1;
	  sb.st_size = directory[i].isDir ? 0 : directory[i].length;
	  sb.st_blocks = (sb.st_size + SECTOR_SIZE * 2 - 1) / (SECTOR_SIZE * 2);
	  filler(buf, name, &sb, 0);
	}
    }

    return 0;
}

/**
 * Helper function for create file/dir on disk
 *
 */
int hw3_create_helper(char *path, mode_t mode, struct fuse_file_info* fi, char c_mode) {
    char block[SECTOR_SIZE * 2];
    int blkPos, start;

    if (strcmp("/", path) == 0) {
        return -EEXIST;
    }

    int posInDir = getBlockIndexFor((char *) path, block, &blkPos);
    if (posInDir >= 0) return posInDir;
    char *slash = NULL;
    slash = strrchr(path, '/');
    char fname[FILENAME_MAX_LENGTH + 1];
    char temp = *slash;
    if (slash != path) {
        strcpy(fname, slash + 1);
        *slash = 0;
        posInDir = getBlockIndexFor((char*) path, block, &blkPos);
        if (dirent.isDir != 1) return -ENOTDIR;
	start = dirent.start;
    } else {
        strcpy(fname, path + 1);
        start = superblock.root_dirent.start;
    }
    disk->ops->read(disk, start * 2, 2, (void*)directory);
    int i = 0;
    // Find next available space in Directory block
    for (i = 0; i < MAX_DIR; i++) {
        if(directory[i].valid == 0) break;
    }

    // blkPos for 
    int free_blkPos = getFreeFAT();
    directory[i].valid = 1;
    directory[i].isDir = c_mode == 'c' ? 0 : 1;  
    directory[i].mode = mode & 01777;
    directory[i].start = free_blkPos;
    directory[i].length = 0;
    directory[i].uid = getuid();
    directory[i].gid = getgid();
    directory[i].mtime = time(NULL);
    strcpy(directory[i].name, fname);
    
    disk->ops->write(disk, start * 2, 2, (void*)directory);
//     disk->ops->write(disk, free_blkPos * 2, 2, block);
    if(c_mode == 'm'){
      disk->ops->read(disk, free_blkPos * 2, 2, (void*)directory);
      for(i=0; i<MAX_DIR; i++){
	directory[i].valid = 0;
      }
      //memcpy(&block, &directory, sizeof(block));
      disk->ops->write(disk, free_blkPos * 2, 2, (void*)directory);
    }
    
    writeFAT();
    *slash = temp;
    return 0;
}

/* create - create a new file with permissions (mode & 01777)
 *
 * Errors - path resolution, EEXIST
 *
 * If a file or directory of this name already exists, return -EEXIST.
 */
static int hw3_create(const char *path, mode_t mode,
        struct fuse_file_info *fi) {

    return hw3_create_helper((char*) path, mode, NULL, 'c');
}

/* mkdir - create a directory with the given mode.
 * Errors - path resolution, EEXIST
 * Conditions for EEXIST are the same as for create.
 */
static int hw3_mkdir(const char *path, mode_t mode) {
    return hw3_create_helper((char*) path, mode, NULL, 'm');
}

/* unlink - delete a file
 *  Errors - path resolution, ENOENT, EISDIR
 */
static int hw3_unlink(const char *path) {
    char block[SECTOR_SIZE * 2];
    int posInDir, blkPos, start;

    if (strcmp("/", path) == 0) {
        return -EISDIR;
    } else {
        posInDir = getBlockIndexFor((char *) path, block, &blkPos);
        start = dirent.start;
        if (posInDir < 0) return posInDir;
        if (dirent.isDir == 1) return -EISDIR;
    }

    // Make the entry invalid
//     directory[posInDir].valid = 0;
    memset(&directory[posInDir], 0, sizeof(directory[5]));
    int next = cs5600fs_fat[start].next;
    int eof = cs5600fs_fat[start].eof;
    cs5600fs_fat[start].inUse = 0;
    while (eof != 1) {
        start = next;
	next = cs5600fs_fat[start].next;
        eof = cs5600fs_fat[start].eof;
        cs5600fs_fat[start].inUse = 0;
    }
    writeFAT();
    disk->ops->write(disk, blkPos * 2, 2, (void*)directory);

    return 0;
}

/* rmdir - remove a directory
 *  Errors - path resolution, ENOENT, ENOTDIR, ENOTEMPTY
 */
static int hw3_rmdir(const char *path) {
    char block[SECTOR_SIZE * 2];
    int posInDir = 0, blkPos = 0, start = 0, rIndex = 0;

    if (strcmp("/", path) == 0) {
        return -ENOENT;
    } else {
        posInDir = getBlockIndexFor((char *) path, block, &blkPos);
	rIndex = posInDir;
        start = dirent.start;
        if (posInDir < 0) return posInDir;
        if (dirent.isDir != 1) return -ENOTDIR;
    }
    // Read contents of Dir to be removed
    struct cs5600fs_dirent dir[MAX_DIR];
    disk->ops->read(disk, start * 2, 2, (void*)dir);
    // Check if dir is empty
    int i;
    for (i = 0; i < MAX_DIR; i++) {
        if (dir[i].valid == 1) break;
    }
    if (i < MAX_DIR) return -ENOTEMPTY;

    memset(&directory[rIndex], 0, sizeof(directory[rIndex]));
    disk->ops->write(disk, blkPos * 2, 2, (void*)directory);
    cs5600fs_fat[start].inUse = 0;
    writeFAT();

    return 0;
}

/* rename - rename a file or directory
 * Errors - path resolution, ENOENT, EINVAL, EEXIST
 *
 * ENOENT - source does not exist
 * EEXIST - destination already exists
 * EINVAL - source and destination are not in the same directory
 *
 * Note that this is a simplified version of the UNIX rename
 * functionality - see 'man 2 rename' for full semantics. In
 * particular, the full version can move across directories, replace a
 * destination file, and replace an empty directory with a full one.
 */
static int hw3_rename(const char *src_path, const char *dst_path) {
    char block[SECTOR_SIZE * 2];
    int posInDir = 0;

    if (strcmp("/", src_path) == 0) {
        return -ENOTSUP;
    } else {
        int blkPos;
        posInDir = in_same_dir((char *) src_path, (char *) dst_path);
        if (posInDir != 0) return -EINVAL;

        posInDir = getBlockIndexFor((char *) dst_path, block, &blkPos);
        if (posInDir > 0) return -EEXIST;

        posInDir = getBlockIndexFor((char *) src_path, block, &blkPos);
        if (posInDir < 0) return posInDir;

        char *slash = strrchr(dst_path, '/');
	
        strcpy(directory[posInDir].name, slash + 1);
        disk->ops->write(disk, blkPos * 2, 2, (void*)directory);
	printf("Succeeded\n");
    }
    return 0;
}

/* Checks if the rename request is a valid one */
int in_same_dir(char *src_path, char *dst_path) {
    char *p1 = strrchr(src_path, '/');
    char *p2 = strrchr(dst_path, '/');

    if ((src_path - p1) != (dst_path - p2)) return -1;

    char path_src[FILENAME_MAX_LENGTH + 1], path_dst[FILENAME_MAX_LENGTH + 1];

    strcpy(path_src, src_path);
    strcpy(path_dst, dst_path);
    path_src[src_path - p1] = 0;
    path_dst[dst_path - p2] = 0;

    if ((src_path - p1 == 0 && dst_path - p2 == 0) || (strcmp(path_src, path_dst)))
      return 0;
    else
      return -1;
}

/* chmod - change file permissions
 * utime - change access and modification times
 *         (for definition of 'struct utimebuf', see 'man utime')
 *
 * Errors - path resolution, ENOENT.
 */
static int hw3_chmod(const char *path, mode_t mode) {
    char block[SECTOR_SIZE * 2];
    int posInDir = 0;

    if (strcmp("/", path) == 0) {
        return -ENOTSUP;
    } else {
        int blkPos = 0;
        posInDir = getBlockIndexFor((char *) path, block, &blkPos);

        if (posInDir < 0) return posInDir;
	directory[posInDir].mode = mode;
        disk->ops->write(disk, blkPos * 2, 2, (void*)directory);
    }
    return 0;
}

int hw3_utime(const char *path, struct utimbuf *ut) {
    char block[SECTOR_SIZE * 2];
    int posInDir = 0;

    if (strcmp(path, "/") == 0) {
        return -ENOTSUP;
    } else {
        int blkPos;
        posInDir = getBlockIndexFor((char *) path, block, &blkPos);
        if (posInDir < 0) return posInDir;

	directory[posInDir].mtime = ut->modtime;
        disk->ops->write(disk, blkPos * 2, 2, (void*)directory);
    }
    return 0;
}

/* truncate - truncate file to exactly 'len' bytes
 * Errors - path resolution, ENOENT, EISDIR, EINVAL
 *    return EINVAL if len > 0.
 */
static int hw3_truncate(const char *path, off_t len) {
    /* you can cheat by only implementing this for the case of len==0,
     * and an error otherwise.
     */
    if (len != 0)
        return -EINVAL; /* invalid argument */

    char block[SECTOR_SIZE * 2];
    int posInDir = 0, start = 0;

    if (strcmp("/", path) == 0) {
        return -ENOTSUP;
    } else {
        int blkPos;
        posInDir = getBlockIndexFor((char *) path, block, &blkPos);
        if (posInDir < 0) return posInDir;
        if (dirent.isDir) return -EISDIR;
	start = dirent.start;
        // Set validity as 0 for a dirent
	directory[posInDir].valid = 0;
	directory[posInDir].length = 0;
        disk->ops->write(disk, blkPos * 2, 2, (void*)directory);

	int next = cs5600fs_fat[start].next;
//         int next = (*(int *) (cs5600fs_fat + start * 4)) / 4;
	int eof = cs5600fs_fat[start].eof;
//         int eof = ((*(int *) (cs5600fs_fat + start * 4)) / 2) & 1;
        cs5600fs_fat[start].inUse = 1;
// 	(*(int *) (cs5600fs_fat + start * 4)) = 3;
        while (eof == 0) {
            start = next;
	    next = cs5600fs_fat[start].next;
//             next = (*(int *) (cs5600fs_fat + start * 4)) / 4;
	    eof = cs5600fs_fat[start].eof;
//             eof = ((*(int *) (cs5600fs_fat + start * 4)) / 2) & 1;
	    cs5600fs_fat[start].inUse = 0;
//             (*(int *) (cs5600fs_fat + start * 4)) = 0;
        }
    }
    writeFAT();
    return 0;

}

/* read - read data from an open file.
 * should return exactly the number of bytes requested, except:
 *   - if offset >= len, return 0
 *   - on error, return <0
 * Errors - path resolution, ENOENT, EISDIR
 */
static int hw3_read(const char *path, char *buf, size_t len, off_t offset,
        struct fuse_file_info *fi) {
    char block[SECTOR_SIZE * 2];
    int posInDir, start;

    if (strcmp("/", path) == 0) {
        return -EISDIR;
    } else {
        int blkPos = 0;
        posInDir = getBlockIndexFor((char*) path, block, &blkPos);

        if (posInDir < 0) return posInDir;
        start = dirent.start;
        if (dirent.isDir) return -EISDIR;
    }
    if (offset >= dirent.length) return 0;

    int blk_num = offset / (SECTOR_SIZE * 2);
    int blk_offset = offset % (SECTOR_SIZE * 2);
    int i;
    for (i = 0; i < blk_num; i++) {
      start = cs5600fs_fat[start].next;
    }
    int lenTemp = len, retVal = 0;
    if (len + offset > dirent.length) {
        lenTemp = dirent.length - offset;
        retVal = lenTemp;
    }

    while (lenTemp > 0) {
        disk->ops->read(disk, start * 2, 2, block);
        if (lenTemp > SECTOR_SIZE * 2 - blk_offset) {
            memcpy(buf, block + blk_offset, SECTOR_SIZE * 2 - blk_offset);
            buf += SECTOR_SIZE * 2 - blk_offset;
            lenTemp -= SECTOR_SIZE * 2 - blk_offset;
	} else {
            memcpy(buf, block + blk_offset, lenTemp);
            buf += lenTemp;
            lenTemp = 0;
        }
        blk_offset = 0;
	start = cs5600fs_fat[start].next;
    }
    if (offset + len > dirent.length) return dirent.length - offset;

    return len;

}

/* write - write data to a file
 * It should return exactly the number of bytes requested, except on
 * error.
 * Errors - path resolution, ENOENT, EISDIR
 *  return EINVAL if 'offset' is greater than current file length.
 */
static int hw3_write(const char *path, const char *buf, size_t len,
        off_t offset, struct fuse_file_info *fi) {
    int posInDir, blkPos, isEnd, start, length;
    
    char block[SECTOR_SIZE * 2];
    
    if(strcmp("/", path) == 0){
      return -EISDIR;
    } else {
      posInDir = getBlockIndexFor((char*)path, block, &blkPos);
      if(posInDir < 0) return posInDir;
      isEnd = cs5600fs_fat[directory[posInDir].start].eof;
      start = dirent.start;
      length = dirent.length;
      if(dirent.isDir) return -EISDIR;
    }
    if(offset > length) return -ENOTSUP;
    if(offset + len > length){
      directory[posInDir].length = offset + len;
      disk->ops->write(disk, blkPos * 2, 2, (void*)directory);
    }
    
    int blk_num = offset / (SECTOR_SIZE * 2);
    int blk_offset = offset % (SECTOR_SIZE * 2);
    
    int i;
    for(i=0; i<blk_num && isEnd !=1; i++){
      start = (*(int *)(fat + start * 4)) / 4; 
      isEnd = ((*(int *)(fat + start * 4)) / 2) & 1;
    }
    
    int prev;
    if(i < blk_num && isEnd == 1)
    {
	 prev =  start;
         start = getFreeFAT();
         (*(int *)(fat + prev * 4)) = (start * 4) + 1;
    }
    int count;
    disk->ops->read(disk, start * 2, 2, block);
    if(len > SECTOR_SIZE * 2 - blk_offset){
      memcpy(block + blk_offset, buf, SECTOR_SIZE * 2 - blk_offset);
      count = SECTOR_SIZE * 2 - blk_offset;
      buf += SECTOR_SIZE * 2 - blk_offset;
    } else {
      memcpy(block + blk_offset, buf, len);
      count = len;
      buf += len;
    }
    disk->ops->write(disk, start * 2, 2, block);
    
    while( len > count){
      if (isEnd == 0){
	start = (*(int *)(fat + start * 4)) / 4;
	isEnd = ((*(int *)(fat + start * 4)) / 2) & 1;
	
      } else {
	prev = start;
	start = getFreeFAT();
	(*(int *)(fat + prev * 4)) = (start * 4) + 1;
      }
      if( len - count > SECTOR_SIZE * 2){
	disk->ops->write(disk, start * 2, 2, (char*)buf);
	buf += SECTOR_SIZE * 2;
	count += SECTOR_SIZE * 2;
      } else {
	disk->ops->read(disk, start * 2, 2, block);
	memcpy(block, buf, len - count);
	disk->ops->write(disk, start * 2, 2, block);
	buf += len - count;
	count += len - count;
      }
    }
    writeFAT();
    return len;
}

// Get first available free block in FAT

int getFreeFAT() {
    int i = 0;
    for (i = 0; i < superblock.fs_size; i++) {
      if(cs5600fs_fat[i].inUse == 0){ 
	cs5600fs_fat[i].inUse = 1;
	cs5600fs_fat[i].eof = 1;
	return i;
      }
//         entry = *((int *) (cs5600fs_fat + i * 4));
//         if ((entry & 1) == 0) {
//             *((int *) (cs5600fs_fat + i * 4)) = 3;
//             return i;
//         }
    }
    return --i;
}

/* statfs - get file system statistics
 * see 'man 2 statfs' for description of 'struct statvfs'.
 * Errors - none. Needs to work.
 */
static int hw3_statfs(const char *path, struct statvfs *st) {
    /* needs to return the following fields (set others to zero):
     *   f_bsize = BLOCK_SIZE
     *   f_blocks = total image - (superblock + FAT)
     *   f_bfree = f_blocks - blocks used
     *   f_bavail = f_bfree
     *   f_namelen = <whatever your max namelength is>
     *
     * it's OK to calculate this dynamically on the rare occasions
     * when this function is called.
     */
    st->f_bsize = superblock.blk_size;
    int fat_size = superblock.fat_len;
    st->f_blocks = superblock.fs_size - fat_size - 1;
    int i = 0, count = 0;
    int entry;

    for (i = 0; i < fat_size; i++) {
        entry = *((int *) (cs5600fs_fat));
        if (entry) count++;
    }
    st->f_bfree = count;
    st->f_bavail = count;
    st->f_namemax = FILENAME_MAX_LENGTH;
    return 0;
}

/* operations vector. Please don't rename it, as the skeleton code in
 * misc.c assumes it is named 'hw3_ops'.
 */
struct fuse_operations hw3_ops = {
    .init = hw3_init,
    .getattr = hw3_getattr,
    .readdir = hw3_readdir,
    .create = hw3_create,
    .mkdir = hw3_mkdir,
    .unlink = hw3_unlink,
    .rmdir = hw3_rmdir,
    .rename = hw3_rename,
    .chmod = hw3_chmod,
    .utime = hw3_utime,
    .truncate = hw3_truncate,
    .read = hw3_read,
    .write = hw3_write,
    .statfs = hw3_statfs,
};