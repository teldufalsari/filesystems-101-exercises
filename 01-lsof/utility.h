#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/**
   A buffer that can be dynamically resized
   by other functions
*/
typedef struct {
	char* buf;
	ssize_t buf_size;
} dyn_buf;

/**
   Returns 1 if filename consists of digits (and
   therefore may represent a PID), 0 otherwise
*/
int is_pid(const char* filename);

/**
   Reads symlink and stores contents into buf.
   If there is not enough space, the function will resize
   the buffer until it fits all file contents.
   The filename is a relative path to a symlink file, and dirfd is
   a directory descriptor
*/
ssize_t areadlinkat(int dirfd, const char* path, dyn_buf* buf);

/**
   Glues together "/proc/", "[pid]" and "/[filename]" strings
   If file is NULL, the resulting string does not include filename,
   only "/proc/[pid]"
*/
const char* construct_full_path(const char* pid_str, const char* file1, const char* file2);

void report_files_inside_directory(int dirfd, DIR* directory, dyn_buf* buffer, const char* pid_str);