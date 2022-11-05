#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef SDJFLK_JAESFLK_KJ_DS
#include <stdio.h>

void report_file(int inode_nr, char type, const char *name)
{
	printf("[%d] \t %c \t %s\n", inode_nr, type, name);
}

#endif // SDJFLK_JAESFLK_KJ_DS

#include "solution.h"
#include <ext2fs/ext2fs.h>
#include <unistd.h>
#include <sys/stat.h>

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

ssize_t load_block(int imgfd, unsigned block_nr, char* buf, size_t block_size)
{
	return pread_exact(imgfd, buf, block_size, block_size * block_nr);
}

void parse_directory_block(const char* buf, size_t block_size)
{
	size_t offset = 0;
	char name[256] = {};
	while (offset + sizeof(struct ext2_dir_entry) - EXT2_NAME_LEN < block_size) {
		const struct ext2_dir_entry* entry = (const struct ext2_dir_entry*)(buf + offset);
		if (entry->rec_len == 0) { // no more records in this block
			break;
		} else if (entry->inode == 0) { // reserved empty space
			offset += entry->rec_len;
			continue;
		}// normal entry
		// get type
		char type = 0;
		switch (ext2fs_dirent_file_type(entry)) {
			case EXT2_FT_REG_FILE:
				type = 'f';
				break;
			case EXT2_FT_DIR:
				type = 'd';
				break;
			default: // other types are not supported
				type = 'x';
				break;
		}
		// get name in readable format
		int name_len = ext2fs_dirent_name_len(entry);
		memcpy(name, entry->name, name_len);
		name[name_len] = '\0';
		// report
		report_file(entry->inode, type, name);
		// load next 
		offset += entry->rec_len;
	}
}

int parse_indirect_block(int imgfd, const unsigned* indir_block, size_t block_size)
{
	char* buf = malloc(block_size);
	unsigned block_ref_count = block_size / sizeof(unsigned);
	for (unsigned i = 0; i < block_ref_count; i++) {
		if (indir_block[i] == 0)
			break;
		if (load_block(imgfd, indir_block[i], buf, block_size) < 0) {
			free(buf);
			return -errno;
		}
		parse_directory_block(buf, block_size);
	}
	free(buf);
	return 0;
}

int parse_double_indirect_block(int imgfd, const unsigned* dindir_block, size_t block_size) {
	unsigned* buf = malloc(block_size);
	unsigned block_ref_count = block_size / sizeof(unsigned);
	for (unsigned i = 0; i < block_ref_count; i++) {
		if (dindir_block[i] == 0)
			break;
		if (load_block(imgfd, dindir_block[i], (char*)buf, block_size) < 0) {
			free(buf);
			return -errno;
		}
		parse_indirect_block(imgfd, buf, block_size);
	}
	free(buf);
	return 0;
}

int parse_dir_by_inode(int img, size_t block_size, const struct ext2_inode* inode_ptr)
{
	char* buf = malloc(block_size);
	for (unsigned i = 0; i < EXT2_NDIR_BLOCKS; i++) {
		if (inode_ptr->i_block[i] == 0)
			break;
		if (load_block(img, inode_ptr->i_block[i], buf, block_size) < 0) {
			free(buf);
			return -errno;
		}
		parse_directory_block(buf, block_size);
	}
	if (inode_ptr->i_block[EXT2_IND_BLOCK] != 0) {
		if (load_block(img, inode_ptr->i_block[EXT2_IND_BLOCK], buf, block_size) < 0) {
			free(buf);
			return -errno;
		}
		if (parse_indirect_block(img, (unsigned*)buf, block_size) < 0) {
			free(buf);
			return -errno;
		}
	}
	if (inode_ptr->i_block[EXT2_DIND_BLOCK] != 0) {
		if (load_block(img, inode_ptr->i_block[EXT2_DIND_BLOCK], buf, block_size) < 0) {
			free(buf);
			return -errno;
		}
		if (parse_double_indirect_block(img, (unsigned*)buf, block_size) < 0) {
			free(buf);
			return -errno;
		}
	}
	free(buf);
	return 0;
}

int dump_dir(int img, int inode_nr)
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
	return parse_dir_by_inode(img, block_size, &inode);
}
