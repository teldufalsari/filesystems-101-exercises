#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <solution.h>

#include "ext2wrappers.h"

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
