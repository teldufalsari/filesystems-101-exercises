#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <solution.h>

#include <liburing.h>
#include <sys/stat.h>
#include <stdlib.h>

#define ENTRIES 8
#define BUFFER_SIZE 256 * 1024

typedef struct {
	// File descriptors
	// infd < 0 ? it's write : it's read
	int infd;
	int outfd;

	// Data buffer
	char* buffer;
	off_t base;
	off_t total_size;

	/* 
	  N of bytes that operation should read to or write
	  from the *buffer*, starting with the *base*
	 */
	off_t requested_size;
	
	// File offset values
	off_t read_offset;
	off_t write_offset;
} cqe_user_data;

int get_file_size(int fd, off_t* size)
{
	struct stat buf = {};
	int code = fstat(fd, &buf);
	if (code < 0) {
		return -errno;
	} else { // imply fd is always a regular file
		*size = buf.st_size;
		return 0;
	}
}

int queue_read(cqe_user_data* transfer_data, struct io_uring* ring)
{
	struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
	if (sqe == NULL) {
		return -1;
	}
	io_uring_prep_read(sqe, transfer_data->infd, 
		transfer_data->buffer + transfer_data->base,
		transfer_data->requested_size, transfer_data->read_offset);
	io_uring_sqe_set_data(sqe, transfer_data);
	return 0x0;
}

int queue_write(cqe_user_data* transfer_data, struct io_uring* ring)
{
	struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
	if (sqe == NULL) {
		return -1;
	}
	transfer_data->infd = -1; // write query signature
	io_uring_prep_write(sqe, transfer_data->outfd, 
		transfer_data->buffer + transfer_data->base, 
		transfer_data->requested_size, transfer_data->write_offset);
	io_uring_sqe_set_data(sqe, transfer_data);
	return 0x0;
}

int process_queries(struct io_uring* ring, int* in_queue, off_t* size_to_write)
{
	// wait for at least one query
	struct io_uring_cqe* cqe;
	int state = io_uring_wait_cqe(ring, &cqe);
	if (state < 0) {
		return state;
	}
	do {
		cqe_user_data* transfer_data = io_uring_cqe_get_data(cqe);
		if (cqe->res == -EAGAIN) {
			if (transfer_data->infd < 0)
				state = queue_write(transfer_data, ring);
			else 
				state = queue_read(transfer_data, ring);
			if (state < 0)
				return state;
			io_uring_submit(ring);
			io_uring_cqe_seen(ring, cqe);
		} else if (cqe->res < 0) {
			return cqe->res;
		} else if (cqe->res < transfer_data->requested_size) {
			transfer_data->requested_size -= cqe->res;
			transfer_data->base += cqe->res;
			if (transfer_data->infd < 0) {
				transfer_data->write_offset += cqe->res;
				queue_write(transfer_data, ring);
			} else {
				transfer_data->read_offset += cqe->res;
				queue_read(transfer_data, ring);
			}
			io_uring_submit(ring);
			io_uring_cqe_seen(ring, cqe);
		} else {
			if (transfer_data->infd > 0) {
				transfer_data->infd = -1;
				transfer_data->requested_size = transfer_data->total_size;
				queue_write(transfer_data, ring);
				io_uring_submit(ring);
			} else {
				(*in_queue)--;
				*size_to_write -= transfer_data->total_size;
				free(transfer_data);
			}
			io_uring_cqe_seen(ring, cqe);
		}
		state = io_uring_peek_cqe(ring, &cqe);
	} while (state == 0);
	if (state == -EAGAIN)
		state = 0;
	return state;
}

int poll(int in, int out, struct io_uring* ring, off_t size_to_submit)
{
	off_t size_to_write = size_to_submit;
	off_t offset = 0;
	int in_queue = 0, state = 0;
	while (size_to_write > 0) {
		int pending = 0;
		while ((size_to_submit > 0) && (in_queue < ENTRIES)) {
			off_t transfer_size = (size_to_submit > BUFFER_SIZE ? BUFFER_SIZE : size_to_submit);

			cqe_user_data* transfer_data = malloc(sizeof(cqe_user_data) + transfer_size);
			transfer_data->infd = in;
			transfer_data->outfd = out;
			transfer_data->buffer = (char*)(transfer_data + 1);
			transfer_data->base = 0;
			transfer_data->total_size = transfer_size;
			transfer_data->requested_size = transfer_size;
			transfer_data->read_offset = offset;
			transfer_data->write_offset = offset;
			queue_read(transfer_data, ring); // return errno

			pending++;
			in_queue++;
			size_to_submit -= transfer_size;
			offset += transfer_size;
		}
		if (pending) {
			io_uring_submit(ring);
		}
		state = process_queries(ring, &in_queue, &size_to_write);
		if (state < 0) {
			return state;
		}
	}
	return 0x0;
}

int copy(int in, int out)
{
	struct io_uring ring;
	off_t size;
	int code = get_file_size(in, &size);
	if (code < 0) {
		return code;
	}
	code = io_uring_queue_init(ENTRIES, &ring, 0);
	if (code != 0) {
		return code;
	}
	char* buf = (char*)malloc(BUFFER_SIZE);
	code = poll(in, out, &ring, size);
	io_uring_queue_exit(&ring);
	free(buf);
	return code < 0 ? code : 0;
}
