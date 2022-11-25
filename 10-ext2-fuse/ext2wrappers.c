#include "ext2wrappers.h"

//~~~~~~~~~~~~~~~~~~~~~~~ UTILS ~~~~~~~~~~~~~~~~~~~~~~~//

ssize_t pread_exact(int fd, void *buf, size_t count, off_t offset)
{
	ssize_t actually_read = 0;
	while ((size_t)actually_read < count) {
		ssize_t current_read =
		    pread(fd, buf, count, offset + actually_read);
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

ssize_t write_exact(int fd, const void *buf, size_t count)
{
	ssize_t actually_written = 0;
	while ((size_t)actually_written < count) {
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

ssize_t load_inode(
	int imgfd, unsigned inode_nr, 
	const struct ext2_super_block *sb, struct ext2_inode *buf)
{
	struct ext2_group_desc bg_header = { };
	unsigned block_size = get_ext2_block_size(sb->s_log_block_size);
	unsigned bg_number = (inode_nr - 1) / sb->s_inodes_per_group;
	unsigned inode_index = (inode_nr - 1) % sb->s_inodes_per_group;
	/*
	 * Read the block group header and get the inode table offset. 
	 */
	off_t bg_header_offset =
	    SUPERBLOCK_OFFSET + sizeof(*sb) + bg_number * sizeof(bg_header);
	ssize_t read_size = pread_exact(imgfd, &bg_header, sizeof(bg_header),
					bg_header_offset);
	if (read_size < 0)
		return -errno;
	off_t inode_offset =
	    block_size * bg_header.bg_inode_table +
	    inode_index * sb->s_inode_size;
	/*
	 * Read the inode 
	 */
	return pread_exact(imgfd, buf, sizeof(*buf), inode_offset);
}

mode_t ext2mode_to_nix(int ext2_mode)
{
	mode_t nix_mode;
	switch (ext2_mode) {
	case EXT2_FT_REG_FILE:
		nix_mode = S_IFREG;
		break;
	case EXT2_FT_DIR:
		nix_mode = S_IFDIR;
		break;
	case EXT2_FT_CHRDEV:
		nix_mode = S_IFCHR;
		break;
	case EXT2_FT_BLKDEV:
		nix_mode = S_IFBLK;
		break;
	case EXT2_FT_FIFO:
		nix_mode = S_IFIFO;
		break;
	case EXT2_FT_SOCK:
		nix_mode = S_IFSOCK;
		break;
	case EXT2_FT_SYMLINK:
		nix_mode = S_IFLNK;
		break;
	default:
		nix_mode = 0;
		break;
	}
	return nix_mode;
}

//~~~~~~~~~~~~~~~~~~~~~~~ PATH TRAVERSE ~~~~~~~~~~~~~~~~~~~~~~~//

int get_inode_nr(int img, const char* path, const struct ext2_super_block* sb)
{
	if (strcmp(path, "/") == 0) {
		return 2;
	} else {
		// Make a copy of the path
		// It will be modified in a strtok() fashion
		char* path_copy = strdup(path);
		int inode_nr = find_inode_by_path(img, 2, path_copy + 1, sb);
		free(path_copy);
		return inode_nr;
	}
}

int find_inode_by_path(
	int imgfd, int start_inode_nr,
	char *path, const struct ext2_super_block *sb)
{
	int ret;
	struct ext2_inode inode = { };
	size_t block_size = get_ext2_block_size(sb->s_log_block_size);
	if ((ret = load_inode(imgfd, start_inode_nr, sb, &inode)) < 0)
		return ret;
	char *next_path_part = strchr(path, '/');
	if (next_path_part == NULL) {
		// we've reached the final part of the path
		// return file inode number
		return find_entry_inode(imgfd, EXT2_FT_UNKNOWN, path, block_size, &inode);
	} else {
		*(next_path_part++) = '\0';	// step over '/' symbol
		int next_nr =
		    find_entry_inode(imgfd, EXT2_FT_DIR, path, block_size, &inode);
		if (next_nr < 0) {
			return next_nr;
		} else {
			// Let's hope this will be a tail recursive call
			return find_inode_by_path(imgfd, next_nr, next_path_part, sb);
		}
	}
}

int find_entry_inode(
	int imgfd, int type, const char *name, 
	size_t block_size, const struct ext2_inode *inode)
{
	char *buf = malloc(block_size);
	int inode_nr = 0;
	for (unsigned i = 0; i < EXT2_NDIR_BLOCKS; i++) {
		if (inode->i_block[i] == 0) {
			free(buf);
			return -ENOENT;
		}
		if (load_block(imgfd, inode->i_block[i], buf, block_size) < 0) {
			free(buf);
			return -errno;
		}
		inode_nr =
		    find_record_in_dir_block(buf, name, block_size, type);
		/*
		 * After this call {
		 *     inode_nr > 0 => record successfully found -- return record inode number,
		 *     inode_nr == 0 => record not found -- continue,
		 *     inode_nr < 0 => an error occured -- return error code
		 * }; 
		 */
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
		if ((inode_nr = find_record_in_indir_block(
				imgfd, type, (unsigned *)buf, name, block_size)) != 0) {
			free(buf);
			return inode_nr;
		}
	}
	if (inode->i_block[EXT2_DIND_BLOCK] != 0) {
		if (load_block(imgfd, inode->i_block[EXT2_DIND_BLOCK], buf, block_size) < 0) {
			free(buf);
			return -errno;
		}
		if ((inode_nr = find_record_in_dindir_block(
				imgfd, type, (unsigned *)buf, name, block_size)) != 0) {
			free(buf);
			return inode_nr;
		}
	}
	free(buf);
	return -ENOENT;
}

int find_record_in_dir_block(
	const char *buf, const char *name, size_t block_size, int type)
{
	size_t offset = 0;
	while (offset + sizeof(struct ext2_dir_entry) - EXT2_NAME_LEN < block_size) {
		const struct ext2_dir_entry *entry = (const struct ext2_dir_entry *)(buf + offset);
		if (entry->rec_len == 0) {	// no more records in this block
			return 0;
		} else if (entry->inode == 0) {	// reserved empty space
			offset += entry->rec_len;
			continue;
		}
		// normal entry:
		unsigned name_len = (unsigned)ext2fs_dirent_name_len(entry);
		if (name_len == strlen(name)) {
			if (strncmp(name, entry->name, name_len) == 0) {
				// this is the entry we are looking for
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

int find_record_in_indir_block(
	int imgfd, int type, const unsigned *indir_block, 
	const char *name, size_t block_size)
{
	char *buf = malloc(block_size);
	unsigned block_ref_count = block_size / sizeof(unsigned);
	int inode_nr = 0;
	for (unsigned i = 0; i < block_ref_count; i++) {
		if (indir_block[i] == 0) {
			free(buf);
			return 0;
		}
		if (load_block(imgfd, indir_block[i], buf, block_size) < 0) {
			free(buf);
			return -errno;
		}
		if ((inode_nr = find_record_in_dir_block(
				buf, name, block_size, type)) != 0) {
			free(buf);
			return inode_nr;
		}
	}
	free(buf);
	return 0;
}

int find_record_in_dindir_block(
	int imgfd, int type, const unsigned *dindir_block,
	const char *name, size_t block_size)
{
	unsigned *buf = malloc(block_size);
	unsigned block_ref_count = block_size / sizeof(unsigned);
	int inode_nr = 0;
	for (unsigned i = 0; i < block_ref_count; i++) {
		if (dindir_block[i] == 0) {
			free(buf);
			return 0;
		}
		if (load_block(imgfd, dindir_block[i], (char *)buf, block_size) < 0) {
			free(buf);
			return -errno;
		}
		if ((inode_nr = find_record_in_indir_block(
				imgfd, type, buf, name, block_size)) != 0) {
			free(buf);
			return inode_nr;
		}
	}
	free(buf);
	return 0;
}

//~~~~~~~~~~~~~~~~~~~~~~~ READDIR ~~~~~~~~~~~~~~~~~~~~~~~//

void parse_directory_block(
	const char *buf, size_t block_size, 
	void *fuse_buf, fuse_fill_dir_t fill_callback)
{
	size_t offset = 0;
	char name[256] = {};
	struct stat stat = {};
	while (offset + sizeof(struct ext2_dir_entry) - EXT2_NAME_LEN < block_size) {
		const struct ext2_dir_entry *entry = (const struct ext2_dir_entry *)(buf + offset);
		if (entry->rec_len == 0) { // no more records in this block
			break;
		} else if (entry->inode == 0) {	// reserved empty space
			offset += entry->rec_len;
			continue;
		}
		// normal entry
		// get name in readable format
		int name_len = ext2fs_dirent_name_len(entry);
		memcpy(name, entry->name, name_len);
		name[name_len] = '\0';
		// report
		stat.st_ino = entry->inode;
		stat.st_mode = ext2mode_to_nix(ext2fs_dirent_file_type(entry));
		fill_callback(fuse_buf, name, &stat, 0, 0);
		// load next
		offset += entry->rec_len;
	}
}

int parse_indirect_block(
	int imgfd, const unsigned *indir_block, size_t block_size,
	void *fuse_buf, fuse_fill_dir_t fill_callback)
{
	char *buf = malloc(block_size);
	unsigned block_ref_count = block_size / sizeof(unsigned);
	for (unsigned i = 0; i < block_ref_count; i++) {
		if (indir_block[i] == 0)
			break;
		if (load_block(imgfd, indir_block[i], buf, block_size) < 0) {
			free(buf);
			return -errno;
		}
		parse_directory_block(buf, block_size, fuse_buf, fill_callback);
	}
	free(buf);
	return 0;
}

int parse_double_indirect_block(
	int imgfd, const unsigned *dindir_block, 
	size_t block_size, void *fuse_buf, fuse_fill_dir_t fill_callback)
{
	unsigned *buf = malloc(block_size);
	unsigned block_ref_count = block_size / sizeof(unsigned);
	for (unsigned i = 0; i < block_ref_count; i++) {
		if (dindir_block[i] == 0)
			break;
		if (load_block(imgfd, dindir_block[i], (char *)buf, block_size) < 0) {
			free(buf);
			return -errno;
		}
		parse_indirect_block(imgfd, buf, block_size, fuse_buf, fill_callback);
	}
	free(buf);
	return 0;
}

int parse_dir_by_inode(
	int img, size_t block_size, const struct ext2_inode *inode_ptr,
	void *fuse_buf, fuse_fill_dir_t fill_callback)
{
	char *buf = malloc(block_size);
	for (unsigned i = 0; i < EXT2_NDIR_BLOCKS; i++) {
		if (inode_ptr->i_block[i] == 0)
			break;
		if (load_block(img, inode_ptr->i_block[i], buf, block_size) < 0) {
			free(buf);
			return -errno;
		}
		parse_directory_block(buf, block_size, fuse_buf, fill_callback);
	}
	if (inode_ptr->i_block[EXT2_IND_BLOCK] != 0) {
		if (load_block (img, inode_ptr->i_block[EXT2_IND_BLOCK], buf, block_size) < 0) {
			free(buf);
			return -errno;
		}
		if (parse_indirect_block
		    (img, (unsigned *)buf, block_size, fuse_buf, fill_callback) < 0) {
			free(buf);
			return -errno;
		}
	}
	if (inode_ptr->i_block[EXT2_DIND_BLOCK] != 0) {
		if (load_block (img, inode_ptr->i_block[EXT2_DIND_BLOCK], buf, block_size) < 0) {
			free(buf);
			return -errno;
		}
		if (parse_double_indirect_block
		    (img, (unsigned *)buf, block_size, fuse_buf, fill_callback) < 0) {
			free(buf);
			return -errno;
		}
	}
	free(buf);
	return 0;
}

//~~~~~~~~~~~~~~~~~~~~~~~ READ ~~~~~~~~~~~~~~~~~~~~~~~//

int read_indirect(
	int imgfd, unsigned offset, unsigned size, 
	unsigned block_size, char *read_buf, unsigned *indir_block)
{
	char *buf = malloc(block_size);
	int ret = 0;
	unsigned block_ref_count = block_size / sizeof(unsigned);
	unsigned size_left = size;
	unsigned start_i = offset / block_size;

	// Deal with the first, uneven block:
	offset %= block_size;
	if (start_i < block_ref_count) { // if, just in case
		unsigned transfer_size = (
			size_left < (block_size - offset) ?
			size_left : (block_size - offset));
		if (indir_block[start_i] != 0) {
			ret = pread_exact(
				imgfd, read_buf, transfer_size, 
				indir_block[start_i] * block_size + offset);
		} else {
			memset(read_buf, 0, block_size - offset);
		}
		if (ret < 0) {
			free(buf);
			return ret;
		} else {
			size_left -= transfer_size;
			read_buf += transfer_size;
		}
	}

	for (unsigned i = start_i + 1; (i < block_ref_count) && (size_left > 0); i++) {
		unsigned transfer_size = (size_left < block_size ? size_left : block_size);
		if (indir_block[i] != 0) {
			ret = pread_exact(imgfd, read_buf, transfer_size, indir_block[i] * block_size);
		} else {
			memset(read_buf, 0, transfer_size);
		}
		if (ret < 0) {
			free(buf);
			return ret;
		} else {
			size_left -= transfer_size;
			read_buf += transfer_size;
		}
	}
	free(buf);
	return (int)(size - size_left);
}

int read_double_indirect(
	int imgfd, unsigned offset, unsigned size, 
	unsigned block_size, char *read_buf,unsigned *dindir_block)
{
	unsigned *indirect_block_buf = malloc(block_size);
	int ret = 0;
	unsigned block_ref_count = block_size / sizeof(unsigned);
	unsigned max_size_ref_by_indir = block_ref_count * block_size;
	unsigned size_left = size;
	unsigned start_i = offset / max_size_ref_by_indir;

	// Deal with the first, uneven block
	offset %= max_size_ref_by_indir;	// this is offset inside an
	// indirect block, in bytes
	if (start_i < block_ref_count) {
		if (dindir_block[start_i] == 0) { // a hole
			unsigned transfer_size = (
				size_left < (max_size_ref_by_indir - offset) ?
				size_left : (max_size_ref_by_indir -offset));
			memset(read_buf, 0, transfer_size);
			size_left -= transfer_size;
			read_buf += transfer_size;
		} else { // a regular block
			ret = pread_exact(
				imgfd, indirect_block_buf, block_size,
				block_size * dindir_block[start_i]);
			if (ret > 0)
				ret = read_indirect(
					imgfd, offset, size_left, block_size,
					read_buf, indirect_block_buf);
			if (ret < 0) {
				free(indirect_block_buf);
				return ret;
			} else {
				size_left -= ret;
				read_buf += ret;
			}
		}
	}

	// Read remaining blocks
	for (unsigned i = start_i + 1;
	     (i < block_ref_count) && (size_left > 0); i++) {
		if (dindir_block[i] == 0) { // a hole
			unsigned transfer_size = (
				size_left < max_size_ref_by_indir ?
				size_left : max_size_ref_by_indir);
			memset(read_buf, 0, transfer_size);
			size_left -= transfer_size;
			read_buf += transfer_size;
		} else { // a regular block
			ret = pread_exact(imgfd, indirect_block_buf, block_size, block_size * dindir_block[i]);
			if (ret > 0)
				// copy_indirect returns copied size
				ret = read_indirect(imgfd, 0, size_left, block_size, read_buf, indirect_block_buf);
			if (ret < 0) {
				free(indirect_block_buf);
				return ret;
			} else {
				size_left -= ret;
				read_buf += ret;
			}
		}
	}
	free(indirect_block_buf);
	return 0;
}

int read_by_inode(
	int imgfd, const struct ext2_inode *inode_ptr,
	unsigned block_size, char *read_buf, off_t offset, size_t buf_size)
{
	// deal with situations when EOF is reached when reading or when
	// offset is past EOF
	if (offset > inode_ptr->i_size) {
		// return all null
		memset(read_buf, 0, buf_size);
		return buf_size;
	} else if (buf_size > (size_t)(inode_ptr->i_size - offset)) {
		buf_size = inode_ptr->i_size - offset;
	}
	// from now assume buf_size > 0
	unsigned size_left = buf_size;
	int ret = 0;
	char *buf = malloc(block_size);
	unsigned block_ref_count = block_size / sizeof(unsigned);
	unsigned max_size_ref_by_indir = block_ref_count * block_size;
	unsigned start_i = 0;

	// Find where to start
	if (offset < EXT2_NDIR_BLOCKS * block_size) {
		// start reading from a direct block
		start_i = offset / block_size;
		offset %= block_size;
		goto read_direct;
	}
	// skip all direct blocks
	offset -= EXT2_NDIR_BLOCKS * block_size;
	if (offset < max_size_ref_by_indir) {
		start_i = EXT2_IND_BLOCK;
		offset %= max_size_ref_by_indir;
		goto read_indirect;
	} else {
		start_i = EXT2_DIND_BLOCK;
		offset -= max_size_ref_by_indir;
		goto read_double_indirect;
	}

 read_direct: ;
	// Copy all data in direct blocks
	// Deal with the first, uneven block
	unsigned transfer_size = (
		size_left < (block_size - offset) ?
		size_left : (block_size - offset));
	if (inode_ptr->i_block[start_i] == 0) {
		memset(read_buf, 0, block_size - offset);
	} else {
		ret = pread_exact(
			imgfd, read_buf, transfer_size,
			inode_ptr->i_block[start_i] * block_size + offset);
	}
	if (ret < 0) {
		free(buf);
		return ret;
	} else {
		size_left -= transfer_size;
		read_buf += transfer_size;
	}
	// Copy even blocks with zero offset
	for (unsigned i = start_i + 1; (i < EXT2_NDIR_BLOCKS) && (size_left > 0); i++) {
		unsigned transfer_size = (
			size_left < block_size ?
			size_left : block_size);
		if (inode_ptr->i_block[i] == 0) {	// a hole
			memset(read_buf, 0, transfer_size);
		} else {
			ret = pread_exact(imgfd, read_buf, transfer_size, inode_ptr->i_block[i]);
		}
		if (ret < 0) {
			free(buf);
			return ret;
		}
		size_left -= transfer_size;
		read_buf += transfer_size;
	}

	if (offset != 0)
		offset = 0;
 read_indirect: ;
	// If anything left, copy data from blocks pointed by the indirect
	// block
	if (size_left > 0) {
		if (inode_ptr->i_block[EXT2_IND_BLOCK] == 0) {	// A hole
			unsigned transfer_size = (
				size_left < (max_size_ref_by_indir - offset) ?
				size_left : (max_size_ref_by_indir - offset));
			memset(read_buf, 0, transfer_size);
			size_left -= transfer_size;
			read_buf += transfer_size;
		} else {
			pread_exact(
				imgfd, buf, block_size,
				block_size * inode_ptr->i_block[EXT2_IND_BLOCK]);
			// copy_indirect returns copied size
			ret = read_indirect(
				imgfd, offset, size_left,block_size, read_buf, (unsigned*)buf);
			if (ret > 0) {
				size_left -= ret;
				read_buf += ret;
			} else {
				free(buf);
				return ret;
			}
		}
	}

	if (offset != 0)
		offset = 0;
 read_double_indirect: ;
	// If anything left, copy data from blocks pointed by the double
	// indirect block
	if (size_left > 0) {
		if (inode_ptr->i_block[EXT2_DIND_BLOCK] == 0) {	// File has a
			// "tail" of zeros
			memset(read_buf, 0, size_left);
		} else {
			pread_exact(
				imgfd, buf, block_size, 
				block_size * inode_ptr->i_block[EXT2_DIND_BLOCK]);
			ret = read_double_indirect(
				imgfd, offset, size_left, block_size, read_buf, (unsigned*)buf);
		}
	}
	// Triple indirect blocks are not supported, we've done all we could
	free(buf);
	if (ret < 0)
		return ret;
	return buf_size;
}
