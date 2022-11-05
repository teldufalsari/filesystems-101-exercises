#include <ext2wrappers.h>

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

int copy_block(int imgfd, int outfd, off_t file_offset, size_t size, char* buf)
{
	if (pread_exact(imgfd, buf, size, file_offset) < 0)
		return -errno;
	if (write_exact(outfd, buf, size) < 0)
		return -errno;
	return 0;
}

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

int find_inode_by_path(int imgfd, int start_inode_nr, char* path, const struct ext2_super_block* sb)
{
	int ret;
	struct ext2_inode inode = {};
	size_t block_size = get_ext2_block_size(sb->s_log_block_size);
	if ((ret = load_inode(imgfd, start_inode_nr, sb, &inode)) < 0)
		return ret;
	char* next_path_part = strchr(path, '/');
	if (next_path_part == NULL) { // we've reached the final part of the path
		// return file inode number
		return find_entry_inode(imgfd, EXT2_FT_UNKNOWN, path, block_size, &inode);
	} else {
		// malloc a separate string for the entry name only
		//char* name = strndup(path, (size_t)(next_path_part - path));
		*(next_path_part++) = '\0'; // step over '/' symbol
		int next_nr = find_entry_inode(imgfd, EXT2_FT_DIR, path, block_size, &inode);
		//free(name);
		if (next_nr < 0) {
			return next_nr;
		} else {
			// Let's hope this will be a tail recursive call
			return find_inode_by_path(imgfd, next_nr, next_path_part, sb);
		}
	}
}

int find_entry_inode(int imgfd, int type, const char* name,
	size_t block_size, const struct ext2_inode* inode)
{
	char* buf = malloc(block_size);
	int inode_nr = 0;
	for (unsigned i = 0; i < EXT2_NDIR_BLOCKS; i++) {
		if (inode->i_block[i] == 0)
			return -ENOENT;
		if (load_block(imgfd, inode->i_block[i], buf, block_size) < 0) {
			free(buf);
			return -errno;
		}
		inode_nr = find_record_in_dir_block(buf, name, block_size, type);
		/* After this call  {
			inode_nr >  0 => record successfully found -- return record inode number,
			inode_nr == 0 => record not found -- continue,
			inode_nr <  0 => an error occured -- return error code
		  }; */
		if (inode_nr != 0) {
			free(buf);
			return inode_nr;
		}
	}
	if (inode->i_block[EXT2_IND_BLOCK] != 0) {
		if (load_block(imgfd, inode->i_block[EXT2_IND_BLOCK], buf, block_size) < 0) {
			free(buf);
			return -errno;
		}
		if ((inode_nr = find_record_in_indir_block(imgfd, type, (unsigned*)buf, name, block_size)) != 0) {
			free(buf);
			return inode_nr;
		}
	}
	if (inode->i_block[EXT2_DIND_BLOCK] != 0) {
		if (load_block(imgfd, inode->i_block[EXT2_DIND_BLOCK], buf, block_size) < 0) {
			free(buf);
			return -errno;
		}
		if ((inode_nr = find_record_in_dindir_block(imgfd, type, (unsigned*)buf, name, block_size)) != 0) {
			free(buf);
			return inode_nr;
		}
	}
	free(buf);
	return -ENOENT;
}

int find_record_in_dir_block(const char* buf, const char* name, size_t block_size, int type)
{
	size_t offset = 0;
	while (offset + sizeof(struct ext2_dir_entry) - EXT2_NAME_LEN < block_size) {
		const struct ext2_dir_entry* entry = (const struct ext2_dir_entry*)(buf + offset);
		if (entry->rec_len == 0) { // no more records in this block
			return 0;
		} else if (entry->inode == 0) { // reserved empty space
			offset += entry->rec_len;
			continue;
		} // normal entry:
		unsigned name_len = (unsigned)ext2fs_dirent_name_len(entry);
		if (name_len == strlen(name)) {
			if (strncmp(name, entry->name, name_len) == 0) { // this is the entry we are looking for
				if ((type == EXT2_FT_DIR) && (type != ext2fs_dirent_file_type(entry)))
					return -ENOTDIR;
				else
					return entry->inode;
			}
		}
		// This is not the entry we are looking for - skip it
		offset += entry->rec_len;
	}
	return 0;
}

int find_record_in_indir_block(int imgfd, int type, const unsigned* indir_block, const char* name, size_t block_size)
{
	char* buf = malloc(block_size);
	unsigned block_ref_count = block_size / sizeof(unsigned);
	int inode_nr = 0;
	for (unsigned i = 0; i < block_ref_count; i++) {
		if (indir_block[i] == 0)
			return 0;
		if (load_block(imgfd, indir_block[i], buf, block_size) < 0) {
			free(buf);
			return -errno;
		}
		if ((inode_nr = find_record_in_dir_block(buf, name, block_size, type)) != 0) {
			free(buf);
			return inode_nr;
		}
	}
	free(buf);
	return 0;
}

int find_record_in_dindir_block(int imgfd, int type, const unsigned* dindir_block, const char* name, size_t block_size)
{
	unsigned* buf = malloc(block_size);
	unsigned block_ref_count = block_size / sizeof(unsigned);
	int inode_nr = 0;
	for (unsigned i = 0; i < block_ref_count; i++) {
		if (dindir_block[i] == 0)
			return 0;
		if (load_block(imgfd, dindir_block[i], (char*)buf, block_size) < 0) {
			free(buf);
			return -errno;
		}
		if ((inode_nr = find_record_in_indir_block(imgfd, type, buf, name, block_size)) != 0) {
			free(buf);
			return inode_nr;
		}
	}
	free(buf);
	return 0;
}

ssize_t load_inode(int imgfd, unsigned inode_nr,
	const struct ext2_super_block* sb, struct ext2_inode* buf)
{
	struct ext2_group_desc bg_header = {};
	unsigned block_size = get_ext2_block_size(sb->s_log_block_size);
	unsigned bg_number = (inode_nr - 1) / sb->s_inodes_per_group;
	unsigned inode_index = (inode_nr - 1) % sb->s_inodes_per_group;
	/*
	  Read the block group header and get the inode table offset.
	*/
	off_t bg_header_offset = SUPERBLOCK_OFFSET + sizeof(*sb) + bg_number * sizeof(bg_header);
	ssize_t read_size = pread_exact(imgfd, &bg_header, sizeof(bg_header), bg_header_offset);
	if (read_size < 0)
		return -errno;
	off_t inode_offset = block_size * bg_header.bg_inode_table + inode_index * sb->s_inode_size;
	/*
		Read the inode
	*/
	return pread_exact(imgfd, buf, sizeof(*buf), inode_offset);
}
