#pragma once

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <unistd.h>
#include <ext2fs/ext2fs.h>
#include <string.h>
#include <sys/stat.h>

/**
  Read exactly @count bytes from the file descriptor if possible.
  It may return less than @count bytes, for example, 
  in case it reaches the end of the file.
 */
ssize_t pread_exact(int fd, void* buf, size_t count, off_t offset);

/**
  Write exactly @count bytes to the file descriptor if possible. 
 */
ssize_t write_exact(int fd, const void* buf, size_t count);

int write_zero_block(int outfd, size_t size);

/**
  Read @size bytes from @imgfd at @file_offset and write to @outfd using
  pre-allocated @buf. It returns 0 on success or -errno value for any of the errors
  specified for read, lseek and write.
 */
int copy_block(int imgfd, int outfd, off_t file_offset, size_t size, char* buf);

/**
  Copy blocks pointed by @indir_block from @imgfd to @outfd. @size value may be greater
  than maximum value referenced by an indirect block. Returns size of copied data in bytes.
 */
int copy_indirect(int imgfd, int outfd, unsigned size, unsigned block_size, unsigned* indir_block);

/**
  Copy data from blocks pointed by indirect blocks, which are in turn pointed by @dindir_block. @size
  value may be greater than maximum value referenced by an indirect block. Returns size of copied data
  in bytes.
 */
int copy_double_indirect(int imgfd, int outfd, unsigned size, unsigned block_size, unsigned* dindir_block);

/**
  Copy a regular file from @imgfd, file system image, to @outfd by its inode.
 */
int copy_by_inode(int imgfd, int outfd, unsigned block_size, const struct ext2_inode* inode_ptr);


/**
  Recursively traverses @path and tries to locate corresponding inode.
  For an ablolute path starting with '/' @start_inode_nr should be 2 (root directory inode).
  Note that @path string is not preserved.
  @returns inode number (positive integer) or -ENOENT, -ENOTDIR, I/O errors.
 */
int find_inode_by_path(int imgfd, int start_inode_nr, char* path, const struct ext2_super_block* sb);

/**
  Search for @name in the directory described by @inode. @inode being not a directory results in an UB.
  @returns inode number (positive integer) or -ENOENT, -ENOTDIR, I/O errors.
 */
int find_entry_inode(int imgfd, int type, const char* name, size_t block_size, const struct ext2_inode* inode);

/**
  Load inode number @inode_nr into @buf
*/
ssize_t load_inode(int imgfd, unsigned inode_nr, const struct ext2_super_block* sb, struct ext2_inode* buf);

/**
  Search for @name in the directory direct data block @buf. If @type is EXT2_FT_DIR, it may return
  -ENOTDIR if an entry with the given name is present in the directory but not a directory itself.
  @returns inode number (positive integer), -ENOTDIR, I/O error code or zero if nothing found.
*/
int find_record_in_dir_block(const char* buf, const char* name, size_t block_size, int type);

/**
  Search for @name in the directory indirect data block @indir_block. If @type is EXT2_FT_DIR, it may
  return -ENOTDIR if an entry with the given name is present in the directory but not a directory itself.
  @returns inode number (positive integer), -ENOTDIR, I/O error code or zero if nothing found.
*/
int find_record_in_indir_block(int imgfd, int type, const unsigned* indir_block, const char* name, size_t block_size);

/**
  Search for @name in the directory doub;e indirect data block @dindir_block. If @type is EXT2_FT_DIR, it may 
  return -ENOTDIR if an entry with the given name is present in the directory but not a directory itself.
  @returns inode number (positive integer), -ENOTDIR, I/O error code or zero if nothing found.
*/
int find_record_in_dindir_block(int imgfd, int type, const unsigned* dindir_block, const char* name, size_t block_size);

inline unsigned get_ext2_block_size(unsigned log_block_size)
{
	return 1024 << log_block_size;
}

inline ssize_t load_block(int imgfd, unsigned block_nr, char* buf, size_t block_size)
{
	return pread_exact(imgfd, buf, block_size, block_size * block_nr);
}
