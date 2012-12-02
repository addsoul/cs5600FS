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

struct cs5600fs_entry entry;

char *cs5600fs_fat;

struct cs5600fs_dirent root;

char* strwrd(char*, char*, size_t, char*);
int getBlockIndexFor(char*, char*, int*);

/* init - this is called once by the FUSE framework at startup.
 * This might be a good place to read in the super-block and set up
 * any global variables you need. You don't need to worry about the
 * argument or the return value.
 */
void* hw3_init(struct fuse_conn_info *conn)
{
  char buffer[SECTOR_SIZE * 2];
  disk->ops->read(disk, 0, 2, buffer);
  superblock.magic = *((unsigned int *) buffer);
  superblock.blk_size = *((unsigned int *) (buffer + 4)); // Always 1024
  superblock.fs_size = *((unsigned int *) (buffer + 8)); // 1024-byte blocks
  superblock.fat_len= *((unsigned int *) (buffer + 12)); // 1024-byte blocks
  
  // Construct root_dirent
  root.uid = *((short *) (buffer + 18));
  root.gid = *((short *) (buffer + 20));
  root.mode = *((short *) (buffer + 22)) | S_IFDIR;
  root.mtime = *((unsigned int *) (buffer + 24));
  root.start = *((unsigned int *) (buffer + 28));
  root.length = 0;
  
  superblock.root_dirent = root;
  
  cs5600fs_fat = (char *) malloc(SECTOR_SIZE * 2 * superblock.fat_len);
  
  // Read FAT from superblock
  int i=0;
  for(i; i < superblock.fat_len; i++){
      disk->ops->read(disk, 2 + (i * 2), 2, cs5600fs_fat + (i * SECTOR_SIZE * 2));
  }
   return NULL;
}

/**
  * Get access to the block of directory for specified file/directory
  * eg. /root/home/xyz/abc.txt will result in block corresponding to the
  * /root/home/xyz/ directory
  * @param: path path of the file / directory
  * @param: buffer buffer that stores the block directory 
  * @param: index the block number of the directory in the image
  * 
  * Return the index of the directory corresponding to the given file
  **/ 
  
  int getBlockIndexFor(char* path, char* buffer, int* index){
	char pathFields[50][FILENAME_MAX_LENGTH + 1];
	int fieldCount = 0;
	struct cs5600fs_dirent directory[16];
	char* line = NULL;
	for(fieldCount; fieldCount < 50; fieldCount++){
	    line = strwrd(path, pathFields[fieldCount], FILENAME_MAX_LENGTH + 1, "/");
	    if(line == NULL){
		    break;
	    }
	}
	int i=0, j=0;
	int dirStart = superblock.root_dirent.start;
	int indexStart = 0;
	
	for(i=0; i<fieldCount; i++){
	  // Read Dir data into buffer jq
	  disk->ops->read(disk, dirStart * 2, 2, (void*)directory);
	  
	  for(j; j<=MAX_DIR; j++){
	    // If j=16 Directory/filename not found
	    if(j == MAX_DIR){
	      return -ENOENT;
	    }
	    // If directory or filename is found on the disk
	    if(strcmp(pathFields[j], directory[j].name) == 0){
	      // If directory then stop searching for next name
		if(directory[j].isDir){
		  indexStart = dirStart;
		  dirStart = directory[j].start;
		  break;
		} else {
		  return -ENOTDIR;
		}
	    }
	    
	  }
	}
	*index = indexStart;
	return j;
  }
  
/* find the next word starting at 's', delimited by characters
 * in the string 'delim', and store up to 'len' bytes into *buf
 * returns pointer to immediately after the word, or NULL if done.
 */
char *strwrd(char *s, char *buf, size_t len, char *delim)
{
    s += strspn(s, delim);
    int n = strcspn(s, delim);  /* count the span (spn) of bytes in */
    if (len-1 < n)              /* the complement (c) of *delim */
        n = len-1;
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
static int hw3_getattr(const char *path, struct stat *sb)
{
    char buffer[SECTOR_SIZE * 2];
    int index;
         
    sb->st_dev = 0;
    sb->st_ino = 0;
    sb->st_rdev = 0;
    
    printf("Block Details: \n");
    printf("Path: %s\n", path);
    printf("magic: %d \t", superblock.magic);
    printf("blk_size: %d \t", superblock.blk_size);
    printf("fs_size: %d \t", superblock.fs_size);
    printf("magic: %d \t", superblock.fat_len);
  
    if(strcmp(path, "/") == 0){
      sb->st_mode =  superblock.root_dirent.mode;
      sb->st_size = superblock.root_dirent.length;
      sb->st_blocks = CEILING(sb->st_size/superblock.blk_size);
    } else {
      printf("Blk No: %d\n", getBlockIndexFor(path, buffer, &index));
    }
    printf("");
    
    return 0;
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
		       off_t offset, struct fuse_file_info *fi)
{
    return -EOPNOTSUPP;
}

/* create - create a new file with permissions (mode & 01777)
 *
 * Errors - path resolution, EEXIST
 *
 * If a file or directory of this name already exists, return -EEXIST.
 */
static int hw3_create(const char *path, mode_t mode,
			 struct fuse_file_info *fi)
{
    return -EOPNOTSUPP;
}

/* mkdir - create a directory with the given mode.
 * Errors - path resolution, EEXIST
 * Conditions for EEXIST are the same as for create.
 */ 
static int hw3_mkdir(const char *path, mode_t mode)
{
    return -EOPNOTSUPP;
}

/* unlink - delete a file
 *  Errors - path resolution, ENOENT, EISDIR
 */
static int hw3_unlink(const char *path)
{
    return -EOPNOTSUPP;
}

/* rmdir - remove a directory
 *  Errors - path resolution, ENOENT, ENOTDIR, ENOTEMPTY
 */
static int hw3_rmdir(const char *path)
{
    return -EOPNOTSUPP;
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
static int hw3_rename(const char *src_path, const char *dst_path)
{
    return -EOPNOTSUPP;
}

/* chmod - change file permissions
 * utime - change access and modification times
 *         (for definition of 'struct utimebuf', see 'man utime')
 *
 * Errors - path resolution, ENOENT.
 */
static int hw3_chmod(const char *path, mode_t mode)
{
    return -EOPNOTSUPP;
}
int hw3_utime(const char *path, struct utimbuf *ut)
{
    return -EOPNOTSUPP;
}

/* truncate - truncate file to exactly 'len' bytes
 * Errors - path resolution, ENOENT, EISDIR, EINVAL
 *    return EINVAL if len > 0.
 */
static int hw3_truncate(const char *path, off_t len)
{
    /* you can cheat by only implementing this for the case of len==0,
     * and an error otherwise.
     */
    if (len != 0)
	return -EINVAL;		/* invalid argument */
    
    return -EOPNOTSUPP;
}

/* read - read data from an open file.
 * should return exactly the number of bytes requested, except:
 *   - if offset >= len, return 0
 *   - on error, return <0
 * Errors - path resolution, ENOENT, EISDIR
 */
static int hw3_read(const char *path, char *buf, size_t len, off_t offset,
		    struct fuse_file_info *fi)
{
    return -EOPNOTSUPP;
}

/* write - write data to a file
 * It should return exactly the number of bytes requested, except on
 * error.
 * Errors - path resolution, ENOENT, EISDIR
 *  return EINVAL if 'offset' is greater than current file length.
 */
static int hw3_write(const char *path, const char *buf, size_t len,
		     off_t offset, struct fuse_file_info *fi)
{
    return -EOPNOTSUPP;
}

/* statfs - get file system statistics
 * see 'man 2 statfs' for description of 'struct statvfs'.
 * Errors - none. Needs to work.
 */
static int hw3_statfs(const char *path, struct statvfs *st)
{
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
	
	for(i; i < fat_size ; i++){
		entry = *((int *) (cs5600fs_fat));
	}
    return -EOPNOTSUPP;
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

