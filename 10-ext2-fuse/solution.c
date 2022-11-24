#include <solution.h>

#include <fuse.h>
#include <string.h>
#include <errno.h>

#include "ext2wrappers.h"

int g_img = 0;
unsigned g_block_size = 0;
struct ext2_super_block g_sb = {};


/** Get file attributes.
 *
 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are
 * ignored.	 The 'st_ino' field is ignored except if the 'use_ino'
 * mount option is given.
 */
int ext2_getattr(const char* path, struct stat* stat, struct fuse_file_info* info)
{
	(void)info;
	struct ext2_inode inode = {};

	int inode_nr = get_inode_nr(g_img, path, &g_sb);
	if (inode_nr < 0)
		return inode_nr;

	ssize_t ret = 0;
	if ((ret = load_inode(g_img, inode_nr, &g_sb, &inode)) < 0)
		return ret;

	struct fuse_context* context = fuse_get_context();
	stat->st_nlink = inode.i_links_count;
	stat->st_mode = inode.i_mode;
	stat->st_uid = context->uid;
	stat->st_gid = context->gid;
	stat->st_size = inode.i_size;
	stat->st_atim.tv_sec = inode.i_atime;
	stat->st_atim.tv_nsec = 0;
	stat->st_mtim.tv_sec = inode.i_mtime;
	stat->st_mtim.tv_nsec = 0;
	stat->st_ctim.tv_sec = inode.i_ctime;
	stat->st_ctim.tv_nsec = 0;
	stat->st_ino = inode_nr;
	stat->st_blocks = inode.i_blocks;
	return 0;
}

/** Create a file node
 *
 * This is called for creation of all non-directory, non-symlink
 * nodes.  If the filesystem defines a create() method, then for
 * regular files that will be called instead.
 */
int ext2_mknod(const char* path, mode_t mode, dev_t device)
{
	(void)path;
	(void)mode;
	(void)device;
	return -EROFS;
}

/** Create a directory 
 *
 * Note that the mode argument may not have the type specification
 * bits set, i.e. S_ISDIR(mode) can be false.  To obtain the
 * correct directory type bits use  mode|S_IFDIR
 * */
int ext2_mkdir(const char* path, mode_t mode)
{
	(void)path;
	(void)mode;
	return -EROFS;
}

/** File open operation
 *
 * No creation (O_CREAT, O_EXCL) and by default also no
 * truncation (O_TRUNC) flags will be passed to open(). If an
 * application specifies O_TRUNC, fuse first calls truncate()
 * and then open(). Only if 'atomic_o_trunc' has been
 * specified and kernel version is 2.6.24 or later, O_TRUNC is
 * passed on to open.
 *
 * Unless the 'default_permissions' mount option is given,
 * open should check if the operation is permitted for the
 * given flags. Optionally open may also return an arbitrary
 * filehandle in the fuse_file_info structure, which will be
 * passed to all file operations.
 *
 * Changed in version 2.2
 */
int ext2_open(const char* path, struct fuse_file_info* info)
{
	struct ext2_inode inode = {};
	int inode_nr = get_inode_nr(g_img, path, &g_sb);
	if (inode_nr < 0)
		return inode_nr;

	ssize_t ret = 0;
	if ((ret = load_inode(g_img, inode_nr, &g_sb, &inode)) < 0)
		return ret;
	
	if (((inode.i_mode & S_IFMT) == S_IFDIR) && ((info->flags & O_DIRECTORY) == 0)) {
		return -EISDIR;
	} else if (info->flags & (O_WRONLY | O_RDWR)) {
		return -EROFS;
	} else {
		// Do I need to check permissions?
		return 0;
	}
}

/** Read data from an open file
 *
 * Read should return exactly the number of bytes requested except
 * on EOF or error, otherwise the rest of the data will be
 * substituted with zeroes.	 An exception to this is when the
 * 'direct_io' mount option is specified, in which case the return
 * value of the read system call will reflect the return value of
 * this operation.
 *
 * Changed in version 2.2
 */
int ext2_read(const char* path, char* buf, 
	size_t buf_size, off_t offset, struct fuse_file_info* info)
{
	(void)info;
	struct ext2_inode inode = {};
	int inode_nr = get_inode_nr(g_img, path, &g_sb);
	if (inode_nr < 0)
		return inode_nr;

	ssize_t ret = 0;
	if ((ret = load_inode(g_img, inode_nr, &g_sb, &inode)) < 0)
		return ret;
	ret = read_by_inode(g_img, &inode, g_block_size, buf, offset, buf_size);
	return ret;
}

/** Write data to an open file
 *
 * Write should return exactly the number of bytes requested
 * except on error.	 An exception to this is when the 'direct_io'
 * mount option is specified (see read operation).
 *
 * Changed in version 2.2
 */
int ext2_write(const char* path, const char* buf,
	size_t buf_size, off_t offset, struct fuse_file_info* info)
{
	(void)buf;
	(void)buf_size;
	(void)offset;
	(void)info;

	int inode_nr = get_inode_nr(g_img, path, &g_sb);

	if (inode_nr < 0) {
		return inode_nr;
	} else {
		return -EROFS;
	}
}

/** Open directory
 *
 * Unless the 'default_permissions' mount option is given,
 * this method should check if opendir is permitted for this
 * directory. Optionally opendir may also return an arbitrary
 * filehandle in the fuse_file_info structure, which will be
 * passed to readdir, releasedir and fsyncdir.
 *
 * Introduced in version 2.3
 */
int ext2_opendir(const char* path, struct fuse_file_info* info)
{
	(void)info;
	struct ext2_inode inode;

	int inode_nr = get_inode_nr(g_img, path, &g_sb);
	if (inode_nr < 0)
		return inode_nr;
	
	ssize_t ret = 0;
	if ((ret = load_inode(g_img, inode_nr, &g_sb, &inode)) < 0) {
		return ret;
	}

	if ((inode.i_mode & S_IFMT) != S_IFDIR) {
		return -ENOTDIR;
	} else {
		// Do I need to check permissions?
		return 0;
	}
}

/** Read directory
 *
 * This supersedes the old getdir() interface.  New applications
 * should use this.
 *
 * The filesystem may choose between two modes of operation:
 *
 * 1) The readdir implementation ignores the offset parameter, and
 * passes zero to the filler function's offset.  The filler
 * function will not return '1' (unless an error happens), so the
 * whole directory is read in a single readdir operation.  This
 * works just like the old getdir() method.
 *
 * 2) The readdir implementation keeps track of the offsets of the
 * directory entries.  It uses the offset parameter and always
 * passes non-zero offset to the filler function.  When the buffer
 * is full (or an error happens) the filler function will return
 * '1'.
 *
 * Introduced in version 2.3
 */
int ext2_readdir(const char* path, void* buf, fuse_fill_dir_t fill_callback,
	off_t offset, struct fuse_file_info* info, enum fuse_readdir_flags flags)
{
	(void)offset;
	(void)info;
	(void)flags;

	struct ext2_inode inode = {};
	int inode_nr = get_inode_nr(g_img, path, &g_sb);
	if (inode_nr < 0)
		return inode_nr;

	ssize_t ret = 0;
	if ((ret = load_inode(g_img, inode_nr, &g_sb, &inode)) < 0)
		return ret;
	return parse_dir_by_inode(g_img, g_block_size, &inode, buf, fill_callback);
}

/**
 * Initialize filesystem
 *
 * The return value will passed in the private_data field of
 * fuse_context to all file operations and as a parameter to the
 * destroy() method.
 *
 * Introduced in version 2.3
 * Changed in version 2.6
 */
void* ext2_init(struct fuse_conn_info* conn, struct fuse_config* config)
{
	(void)conn;
	(void)config;
	return NULL;
}

static const struct fuse_operations ext2_ops = {
	.getattr = &ext2_getattr,
	.mknod = &ext2_mknod,
	.mkdir = &ext2_mkdir,
	.open = &ext2_open,
	.read = &ext2_read,
	.write = &ext2_write,
	.opendir = &ext2_opendir,
	.readdir = &ext2_readdir,
	.init = &ext2_init,
};

int ext2fuse(int img, const char *mntp)
{
	g_img = img;
	if (pread_exact(img, &g_sb, sizeof(g_sb), SUPERBLOCK_OFFSET) < 0)
		return -errno;
	g_block_size = get_ext2_block_size(g_sb.s_log_block_size);

	char *argv[] = {"exercise", "-f", (char *)mntp, NULL};
	return fuse_main(3, argv, &ext2_ops, NULL);
}
