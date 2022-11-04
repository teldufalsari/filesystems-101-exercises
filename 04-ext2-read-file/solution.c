#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <solution.h>
#include <ext2fs/ext2fs.h>
#include <unistd.h>

/**
  Read exactly @count bytes from the file descriptor if possible.
  It may return less than @count bytes, for example, 
  in case it reaches the end of the file.
 */
ssize_t pread_exact(int fd, void* buf, size_t count, off_t offset)
{
	ssize_t actually_read = 0;
	while ((size_t)actually_read < count) {
		ssize_t current_read = pread(fd, buf, count, offset + actually_read);
		if (current_read > 0) {
			actually_read += current_read;
			count -= current_read;
		} else if (errno == EINTR) {
			continue;
		} else if (current_read == 0) {
			break;
		} else {
			return -1;
		}
	}
	return actually_read;
}

/**
  Write exactly @count bytes to the file descriptor if possible. 
 */
ssize_t write_exact(int fd, void* buf, size_t count)
{
	ssize_t actually_written = 0;
	while((size_t)actually_written < count) {
		ssize_t current_write = write(fd, buf, count);
		if (current_write > 0) {
			actually_written += current_write;
			count -= current_write;
		} else if (errno == EINTR) {
			continue;
		} else if (current_write == 0) {
			break;
		} else {
			return -1;
		}
	}
	return actually_written;
}

unsigned get_ext2_block_size(unsigned log_block_size)
{
	return 1024 << log_block_size;
}

/**
  Read @size bytes from @imgfd at @file_offset and write to @outfd using
  pre-allocated @buf. It returns 0 on success or -errno value for any of the errors
  specified for read, lseek and write.
 */
int copy_block(int imgfd, int outfd, off_t file_offset, size_t size, char* buf)
{
	if (pread_exact(imgfd, buf, size, file_offset) < 0)
		return -errno;
	if (write_exact(outfd, buf, size) < 0)
		return -errno;
	return 0;
}

/**
  Copy blocks pointed by @indir_block from @imgfd to @outfd. @size value may be greater
  than maximum value referenced by an indirect block. Returns size of copied data in bytes.
 */
int copy_indirect(int imgfd, int outfd, unsigned size, unsigned block_size, unsigned* indir_block)
{
	char* buf = malloc(block_size);
	int ret = 0;
	unsigned block_ref_count = block_size / sizeof(unsigned);
	unsigned size_left = size;
	for (unsigned i = 0; i < block_ref_count; i++) {
		unsigned transfer_size = (size_left < block_size ? size_left : block_size);
		ret = copy_block(imgfd, outfd, indir_block[i] * block_size, transfer_size, buf);
		if (ret < 0) {
			free(buf);
			return ret;
		} else {
			size_left -= transfer_size;
		}
	}
	free(buf);
	return (int)(size - size_left);
}

/**
  Copy data from blocks pointed by indirect blocks, which are in turn pointed by @dindir_block. @size value may be greater
  than maximum value referenced by an indirect block. Returns size of copied data in bytes.
 */
int copy_double_indirect(int imgfd, int outfd, unsigned size, unsigned block_size, unsigned* dindir_block)
{
	unsigned* indirect_block_buf = malloc(block_size);
	int ret = 0;
	unsigned block_ref_count = block_size / sizeof(unsigned);
	unsigned size_left = size;

	for (unsigned i = 0; (i < block_ref_count) && (size_left > 0); i++) {
		ret = pread_exact(imgfd, indirect_block_buf, block_size, block_size * dindir_block[i]);
		if(ret > 0)
			// copy_indirect returns copied size
			ret = copy_indirect(imgfd, outfd, size_left, block_size, indirect_block_buf);

		if (ret < 0) {
			free(indirect_block_buf);
			return ret;
		} else {
			size_left -= ret;
		}
	}
	free(indirect_block_buf);
	return 0;
}

/**
  Copy a regular file from @imgfd, file system image, to @outfd by its inode.
 */
int copy_by_inode(int imgfd, int outfd, unsigned block_size, const struct ext2_inode* inode_ptr)
{
	unsigned size_left = inode_ptr->i_size;
	int ret = 0;
	char* buf = malloc(block_size);

	// Copy all data in direct blocks
	for (unsigned i = 0; i < EXT2_NDIR_BLOCKS; i++) {
		unsigned transfer_size = (size_left < block_size ? size_left : block_size);
		ret = copy_block(imgfd, outfd, (inode_ptr->i_block[i]) * block_size, transfer_size, buf);
		if (ret < 0) {
			free(buf);
			return ret;
		}
		size_left -= transfer_size;
	}
	// If anything left, copy data from blocks pointed by the indirect block
	if (size_left > 0) {
		pread_exact(imgfd, buf, block_size, block_size * inode_ptr->i_block[EXT2_IND_BLOCK]);
		// copy_indirect returns copied size
		ret = copy_indirect(imgfd, outfd, size_left, block_size, (unsigned*)buf);
		if (ret > 0) {
			size_left -= ret;
		} else {
			free(buf);
			return ret;
		}
	}
	// If anything left, copy data from blocks pointed by the double indirect block
	if (size_left) {
		pread_exact(imgfd, buf, block_size, block_size * inode_ptr->i_block[EXT2_DIND_BLOCK]);
		ret = copy_double_indirect(imgfd, outfd, size_left, block_size, (unsigned*)buf);
	}
	// Triple indirect blocks are not supported, we've done all we could
	free(buf);
	if (ret < 0)
		return ret;
	return 0;
}

int dump_file(int img, int inode_nr, int out)
{
	struct ext2_super_block sb = {};
	struct ext2_inode inode = {};
	struct ext2_group_desc bg_header = {};
	ssize_t read_size = 0;
	/* 
	  Read the superblock to obtain block size, inode size, block group 
	  and inode index inside this block group
	*/
	read_size = pread_exact(img, &sb, sizeof(sb), SUPERBLOCK_OFFSET);
	if (read_size < 0)
		return -errno;
	unsigned block_size = get_ext2_block_size(sb.s_log_block_size);
	unsigned bg_number = (inode_nr - 1) / sb.s_inodes_per_group;
	unsigned inode_index = (inode_nr - 1) % sb.s_inodes_per_group;
	/*
	  Read the block group header and get the inode table offset.
	*/
	off_t bg_header_offset = SUPERBLOCK_OFFSET + sizeof(sb) + bg_number * sizeof(bg_header);
	read_size = pread_exact(img, &bg_header, sizeof(bg_header), bg_header_offset);
	if (read_size < 0)
		return -errno;
	off_t inode_offset = block_size * bg_header.bg_inode_table + inode_index * sb.s_inode_size;
	/*
		Read the inode
	*/
	read_size = pread_exact(img, &inode, sizeof(inode), inode_offset);
	if (read_size < 0)
		return -errno;
	return copy_by_inode(img, out, block_size, &inode);
}
