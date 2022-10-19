#include <solution.h>

#include <fuse.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#define NAME "/hello"


/** Get file attributes.
 *
 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are
 * ignored.	 The 'st_ino' field is ignored except if the 'use_ino'
 * mount option is given.
 */
int hello_getattr(const char* path, struct stat* stat, struct fuse_file_info* info)
{
	(void)info;
	if (strcmp(path, "/") == 0) {
		struct fuse_context* context = fuse_get_context();
		stat->st_nlink = 2;
		stat->st_mode = S_IFDIR | 0775;
		stat->st_uid = context->uid;
		stat->st_gid = context->gid;
		stat->st_size = 0;
		stat->st_atim.tv_sec = 1666173107;
		stat->st_atim.tv_nsec = 0;
		stat->st_mtim.tv_sec = 1666173107;
		stat->st_mtim.tv_nsec = 0;
		stat->st_ctim.tv_sec = 1666173107;
		stat->st_ctim.tv_nsec = 0;
		return 0;
	} else if (strcmp(path, NAME) == 0) {
		struct fuse_context* context = fuse_get_context();
		stat->st_nlink = 1;
		stat->st_mode = S_IFREG | 0400;
		stat->st_uid = context->uid;
		stat->st_gid = context->gid;
		stat->st_size = 64;
		stat->st_atim.tv_sec = 1666173107;
		stat->st_atim.tv_nsec = 0;
		stat->st_mtim.tv_sec = 1666173107;
		stat->st_mtim.tv_nsec = 0;
		stat->st_ctim.tv_sec = 1666173107;
		stat->st_ctim.tv_nsec = 0;
		return 0;
	} else {
		return -ENOENT;
	}
}

/** Create a file node
 *
 * This is called for creation of all non-directory, non-symlink
 * nodes.  If the filesystem defines a create() method, then for
 * regular files that will be called instead.
 */
int hello_mknod(const char* path, mode_t mode, dev_t device)
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
int hello_mkdir(const char* path, mode_t mode)
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
int hello_open(const char* path, struct fuse_file_info* info)
{
	if (strcmp(path, "/") == 0) {
		if ((info->flags & O_DIRECTORY) == 0) {
			return -EISDIR;
		} else return 0;
	} else if (strcmp(path, NAME) == 0) {
		if (info->flags & (O_WRONLY | O_RDWR)) {
			return -EROFS;
		} else return 0;
	} else return -ENOENT;
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
int hello_read(const char* path, char* buf, 
	size_t buf_size, off_t offset, struct fuse_file_info* info)
{
	(void)info;
	if (strcmp(path, NAME) == 0) {
		memset(buf, '\0', buf_size);
		struct fuse_context* context = fuse_get_context();
		char teh_file[64];
		memset(teh_file, '\0', 64);
		snprintf(teh_file, 64, "hello, %d\n", context->pid);
		size_t n = strlen(teh_file);
		if ((size_t)offset >= n) {
			return 0;
		} else {
			n -= offset;
			memcpy(buf, teh_file + offset, n);
			return n;
		}
	} else return -ENOENT;
}

/** Write data to an open file
 *
 * Write should return exactly the number of bytes requested
 * except on error.	 An exception to this is when the 'direct_io'
 * mount option is specified (see read operation).
 *
 * Changed in version 2.2
 */
int hello_write(const char* path, const char* buf,
	size_t buf_size, off_t offset, struct fuse_file_info* info)
{
	(void)buf;
	(void)buf_size;
	(void)offset;
	(void)info;
	if (strcmp(path, NAME) == 0) {
		return -EROFS;
	} else return -ENOENT;
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
int hello_opendir(const char* path, struct fuse_file_info* info)
{
	(void)info;
	if (strcmp(path, "/") == 0) {
		return 0;
	} else return -ENOENT;
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
int hello_readdir(const char* path, void* buf, fuse_fill_dir_t fill_callback,
	off_t offset, struct fuse_file_info* info, enum fuse_readdir_flags flags)
{
	(void)offset;
	(void)info;
	(void)flags;
	if (strcmp(path, "/") == 0) {
		fill_callback(buf, ".", NULL, 0, 0);
		fill_callback(buf, "..", NULL, 0, 0);
		fill_callback(buf, "hello", NULL, 0, 0);
		return 0;
	} else return -ENOENT;
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
void* hello_init(struct fuse_conn_info* conn, struct fuse_config* config)
{
	(void)conn;
	(void)config;
	return NULL;
}


static const struct fuse_operations hellofs_ops = {
	.getattr = &hello_getattr,
	.mknod = &hello_mknod,
	.mkdir = &hello_mkdir,
	.open = &hello_open,
	.read = &hello_read,
	.write = &hello_write,
	.opendir = &hello_opendir,
	.readdir = &hello_readdir,
	.init = &hello_init,
};

int helloworld(const char *mntp)
{
	char *argv[] = {"exercise", "-f", (char *)mntp, NULL};
	return fuse_main(3, argv, &hellofs_ops, NULL);
}
