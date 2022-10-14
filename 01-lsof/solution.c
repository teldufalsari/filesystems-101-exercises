#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <solution.h>
#include "utility.h"

void lsof(void)
{
	int proc_dirfd = open("/proc", O_DIRECTORY);
	if (proc_dirfd < 0) {
		report_error("/proc", errno);
		return;
	}
	DIR* proc_dir_struct = fdopendir(proc_dirfd);
	if (proc_dir_struct == NULL) {
		report_error("/proc", errno);
		return;
	}
	dyn_buf path_buffer = {calloc(256, sizeof(char)), 256 * sizeof(char)};
	struct dirent* cur_entry = NULL;
	while ((cur_entry = readdir(proc_dir_struct)) != NULL) {
		if (is_pid(cur_entry->d_name)) {
			int fd_dirfd = open(construct_full_path(cur_entry->d_name, "fd", NULL), O_DIRECTORY);
			if (fd_dirfd < 0) {
				report_error(construct_full_path(cur_entry->d_name, "fd", NULL), errno);
				continue;
			}
			DIR* fd_dir_struct = fdopendir(fd_dirfd);
			if (fd_dir_struct == NULL) {
				report_error(construct_full_path(cur_entry->d_name, "fd", NULL), errno);
				close(fd_dirfd);
				continue;
			}
			report_files_inside_directory(fd_dirfd, fd_dir_struct, &path_buffer, cur_entry->d_name);
			closedir(fd_dir_struct);
			close(fd_dirfd);
		}
	}
	free(path_buffer.buf);
	closedir(proc_dir_struct);
	close(proc_dirfd);
}

void report_files_inside_directory(int dirfd, DIR* directory, dyn_buf* buffer, const char* pid_str)
{
	struct dirent*  cur_entry = NULL;
	ssize_t lnk_read_size = 0;
	while ((cur_entry = readdir(directory)) != NULL) {
		lnk_read_size = areadlinkat(dirfd, cur_entry->d_name, buffer);
		if (lnk_read_size < 0) {
			report_error(construct_full_path(pid_str, "fd", cur_entry->d_name), errno);
			continue;
		}
		if (buffer->buf[0] == '/')
			report_file(buffer->buf);
	}
}

int is_pid(const char* filename)
{
	unsigned cursor = 0;
	while ((filename[cursor] >= '0') && (filename[cursor] <= '9'))
		cursor++;
	return filename[cursor] == '\0' ? 1 : 0;
}

ssize_t areadlinkat(int dirfd, const char* path, dyn_buf* buf)
{
	ssize_t read_size = readlinkat(dirfd, path, buf->buf, buf->buf_size);
	while (read_size >= buf->buf_size) {
		free(buf->buf);
		buf->buf_size *= 2;
		buf->buf = (char*) malloc(buf->buf_size);
		read_size = readlinkat(dirfd, path, buf->buf, buf->buf_size);
	}
	if (read_size > 0)
		buf->buf[read_size] = '\0';
	return read_size;
}

const char* construct_full_path(const char* pid_str, const char* file1, const char* file2)
{
	static char path_buf[256];
	if (file1 == NULL)
		snprintf(path_buf, 256, "/proc/%s", pid_str);
	else if (file2 == NULL)
		snprintf(path_buf, 256, "/proc/%s/%s", pid_str, file1);
	else
		snprintf(path_buf, 256, "/proc/%s/%s/%s", pid_str, file1, file2);
	return path_buf;
}