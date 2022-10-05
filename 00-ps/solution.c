#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <solution.h>
#include "utility.h"

void ps(void)
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
	struct dirent* cur_entry = NULL;
	dyn_buf exe_buf = {(char*) malloc(256), 256};
	dyn_buf argv_buf = {(char*) malloc(256), 256};
	dyn_buf envp_buf = {(char*) malloc(1024), 1024};
	str_list arg_list = {(char**) malloc (32 * sizeof(char*)), 32};
	str_list env_list = {(char**) malloc(64 * sizeof(char*)), 64};
	while ((cur_entry = readdir(proc_dir_struct)) != NULL) {
		if (is_pid(cur_entry->d_name)) {
			int dirfd = openat(proc_dirfd, cur_entry->d_name, O_DIRECTORY);
			if (dirfd < 0) {
				report_error(construct_full_path(cur_entry->d_name, NULL), errno);
				continue;
			}
			pid_t cur_pid = strtol(cur_entry->d_name, NULL, 10);
			ssize_t exe_size = areadlinkat(dirfd, "exe", &exe_buf);
			if (exe_size < 0) {
				report_error(construct_full_path(cur_entry->d_name, "exe"), errno);
				continue;
			}
			ssize_t argv_size = areadat(dirfd, "cmdline", &argv_buf);
			if (argv_size < 0) {
				report_error(construct_full_path(cur_entry->d_name, "cmdline"), errno);
				continue;
			}
			ssize_t envp_size = areadat(dirfd, "environ", &envp_buf);
			if (envp_size < 0) {
				report_error(construct_full_path(cur_entry->d_name, "environ"), errno);
				continue;
			}
			tokenize(argv_buf.buf, argv_size, &arg_list);
			tokenize(envp_buf.buf, envp_size, &env_list);
			report_process(cur_pid, exe_buf.buf, arg_list.strings, env_list.strings);
		}
	}
	free(exe_buf.buf);
	free(argv_buf.buf);
	free(envp_buf.buf);
	free(arg_list.strings);
	free(env_list.strings);
	closedir(proc_dir_struct);
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

ssize_t areadat(int dirfd, const char* filename, dyn_buf* buf)
{
	int filefd = openat(dirfd, filename, O_RDONLY);
	if (filefd < 0)
		return -1;
	ssize_t read_size = 0, total_read = 0;
	while ((read_size = read(filefd, buf->buf + total_read, buf->buf_size - total_read)) == (buf->buf_size - total_read)) {
		total_read += read_size;
		ssize_t new_buf_size = buf->buf_size * 2;
		char* new_buf = (char*) realloc(buf->buf, new_buf_size);
		if (new_buf == NULL)
			return -1;
		buf->buf = new_buf;
		buf->buf_size = new_buf_size;
	}
	if (read_size < 0)
		return -1;
	total_read += read_size;
	return total_read;
}

const char* construct_full_path(const char* pid_str, const char* file)
{
	static char path_buf[256];
	if (file == NULL)
		snprintf(path_buf, 256, "/proc/%s", pid_str);
	else
		snprintf(path_buf, 256, "/proc/%s/%s", pid_str, file);
	return path_buf;
}

ssize_t tokenize(char* buf, ssize_t buf_size, str_list* list)
{
	ssize_t str_count = 0;
	char* cursor = buf;
	for (ssize_t i = 0; i < buf_size; i++) {
		if (buf[i] == '\n')
			buf[i] = '\0';
		if (buf[i] == '\0') {
			str_count++;
			while (str_count + 1 >= list->size) {
				ssize_t new_size = list->size * 2;
				char** new_strings = realloc(list->strings, new_size * (sizeof(char*)));
				if (new_strings == NULL)
					return -1;
				list->strings = new_strings;
				list->size = new_size;
			}
			list->strings[str_count - 1] = cursor;
			cursor = buf + i + 1;
		}
	}
	list->strings[str_count] = NULL;
	return str_count;
}