#pragma once

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <unistd.h>
#include <ext2fs/ext2fs.h>
#include <string.h>
#include <sys/stat.h>
#include <fuse.h>


//~~~~~~~~~~~~~~~~~~~~~~~ UTILS ~~~~~~~~~~~~~~~~~~~~~~~//

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

/**
  Load inode number @inode_nr into @buf
*/
ssize_t load_inode(int imgfd, unsigned inode_nr, const struct ext2_super_block* sb, struct ext2_inode* buf);


inline unsigned get_ext2_block_size(unsigned log_block_size)
{
	return 1024 << log_block_size;
}

inline ssize_t load_block(int imgfd, unsigned block_nr, char* buf, size_t block_size)
{
	return pread_exact(imgfd, buf, block_size, block_size * block_nr);
}

//~~~~~~~~~~~~~~~~~~~~~~~ PATH TRAVERSE ~~~~~~~~~~~~~~~~~~~~~~~//


/**
 * Find inode number corresponding to @path
 * @returns inode number (positive integer) or -ENOENT, -ENOTDIR, I/O errors.
 */
int get_inode_nr(int img, const char* path, const struct ext2_super_block* sb);

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


//~~~~~~~~~~~~~~~~~~~~~~~ READDIR ~~~~~~~~~~~~~~~~~~~~~~~//

/**
 * Calls @fill_callback with @fuse_buf arg at each record in the directory
 * pointed by @inode_ptr
 * 
 * @returns 0 on success or -ENOTDIR, -ENOENT or errors returned by read()
 */
int parse_dir_by_inode(
	int img, size_t block_size, const struct ext2_inode* inode_ptr,
	void* fuse_buf, fuse_fill_dir_t fill_callback);

/**
 * Calls @fill_callback with @fuse_buf arg at each entry found in
 * a double indirect block.
 *  @returns 0 on success or -ENOTDIR, -ENOENT or errors returned by read()
 */
int parse_double_indirect_block(
	int imgfd, const unsigned* dindir_block, size_t block_size,
	void* fuse_buf, fuse_fill_dir_t fill_callback);

/**
 * Calls @fill_callback with @fuse_buf arg at each entry found in
 * a single indirect block.
 *  @returns 0 on success or -ENOTDIR, -ENOENT or errors returned by read()
 */
int parse_indirect_block(
	int imgfd, const unsigned* indir_block,
	size_t block_size, void* fuse_buf, fuse_fill_dir_t fill_callback);

/**
 * Calls @fill_callback with @fuse_buf arg at each entry found in
 * a direct block.
 *  @returns 0 on success or -ENOTDIR, -ENOENT or errors returned by read()
 */
void parse_directory_block(
	const char* buf, size_t block_size,
	void* fuse_buf, fuse_fill_dir_t fill_callback);


//~~~~~~~~~~~~~~~~~~~~~~~ READ ~~~~~~~~~~~~~~~~~~~~~~~//

/**
 * Reads @size bytes from the file pointed by @inode_ptr
 * to @buf beginning with @offset.
 * 
 * @returns Size read of -errno (errors returned by read())
 */
int read_by_inode(int img, const struct ext2_inode* inode_ptr, 
	unsigned block_size, char* buf, off_t offset, size_t size);

/**
 * Reads up to @size bytes from a double indirect block.
 * The @offset is relative to the beginning of the block.
 * 
 * @returns Size read of -errno (errors returned by read())
 */
int read_double_indirect(
	int imgfd, unsigned offset, unsigned size, 
	unsigned block_size, char *read_buf,unsigned *dindir_block);

/**
 * Reads up to max size referenced by a single inderect block,
 * but not more that @size.
 * 
 * @returns Size read or -errno (errors returned by read())
 */
int read_indirect(
	int imgfd, unsigned offset, unsigned size, 
	unsigned block_size, char *read_buf, unsigned *indir_block);
