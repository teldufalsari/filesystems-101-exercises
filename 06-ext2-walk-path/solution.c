#include <solution.h>
#include <ext2wrappers.h>

int dump_file(int img, const char *path, int out)
{
	(void) img;
	(void) path;
	(void) out;
	struct ext2_super_block sb = {};
	struct ext2_inode inode = {};
	if (pread_exact(img, &sb, sizeof(sb), SUPERBLOCK_OFFSET) < 0)
		return -errno;
	// Make a copy of the path
	// It will be modified in a strtok() fashion
	char* path_copy = strdup(path);
	int inode_nr = find_inode_by_path(img, 2, path_copy + 1, &sb);
	free(path_copy);
	if (inode_nr < 0)
		return inode_nr;
	ssize_t ret = 0;
	if ((ret = load_inode(img, inode_nr, &sb, &inode)) < 0)
		return ret;
	ssize_t block_size = get_ext2_block_size(sb.s_log_block_size);
	return copy_by_inode(img, out, block_size, &inode);
}
