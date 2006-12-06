#define _GNU_SOURCE
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <signal.h>
#include <assert.h>
#include <err.h>

#include "stdrusty.h"

/* Stevens. */
void daemonize(void)
{
	pid_t pid;

	/* Separate from our parent via fork, so init inherits us. */
	if ((pid = fork()) < 0)
		err(1, "Failed to fork daemon");
	if (pid != 0)
		exit(0);

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	/* Session leader so ^C doesn't whack us. */
	setsid();
	/* Move off any mount points we might be in. */
	chdir("/");
	/* Discard our parent's old-fashioned umask prejudices. */
	umask(0);
}


/* This version adds one byte (for nul term) */
void *grab_file(const char *filename, unsigned long *size)
{
	unsigned int max = 16384;
	int ret, fd;
	void *buffer;

	if (streq(filename, "-"))
		fd = dup(STDIN_FILENO);
	else
		fd = open(filename, O_RDONLY, 0);

	if (fd < 0)
		return NULL;

	buffer = malloc(max+1);
	*size = 0;
	while ((ret = read(fd, buffer + *size, max - *size)) > 0) {
		*size += ret;
		if (*size == max)
			buffer = realloc(buffer, max *= 2 + 1);
	}
	if (ret < 0) {
		free(buffer);
		buffer = NULL;
	} else
		((char *)buffer)[*size] = '\0';
	close(fd);
	return buffer;
}

void release_file(void *data, unsigned long size)
{
	free(data);
}

struct signal_map
{
	/* [0] for reading, [1] for writing. */
	int fds[2];
};
static struct signal_map signal_map[_NSIG];

static void send_to_fd(int signo)
{
	int saved_errno = errno;
	write(signal_map[signo].fds[1], "1", 1);
	errno = saved_errno;
}

/* Signal handling: returns fd to listen on. */
int signal_to_fd(int signo)
{
	int flags, saved_errno;

	if (signo <= 0 || signo >= _NSIG) {
		errno = EINVAL;
		return -1;
	}
	if (signal_map[signo].fds[0] != -1) {
		errno = EBUSY;
		return -1;
	}

	if (pipe(signal_map[signo].fds) != 0)
		goto abort;

	/* Set to nonblocking in case of signal flood. */
	flags = fcntl(signal_map[signo].fds[1], F_GETFL);
	if (flags == -1)
		goto close;
	if (fcntl(signal_map[signo].fds[1], F_SETFL, flags|O_NONBLOCK) != 0)
		goto close;

	if (signal(signo, send_to_fd) == SIG_ERR)
		goto close;

	return signal_map[signo].fds[0];

close:
	saved_errno = errno;
	close(signal_map[signo].fds[1]);
	close(signal_map[signo].fds[0]);
	errno = saved_errno;
abort:
	signal_map[signo].fds[0] = -1;
	return -1;
	
}

void close_signal(int fd)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(signal_map); i++) {
		if (signal_map[i].fds[0] == fd) {
			signal(i, SIG_DFL);
			close(signal_map[i].fds[1]);
			close(signal_map[i].fds[0]);
			signal_map[i].fds[0] = -1;
			break;
		}
	}
}

bool write_all(int fd, const void *data, unsigned long size)
{
	while (size) {
		int done;

		done = write(fd, data, size);
		if (done < 0 && errno == EINTR)
			continue;
		if (done <= 0)
			return false;
		data += done;
		size -= done;
	}

	return true;
}

bool read_all(int fd, void *data, unsigned long size)
{
	while (size) {
		int done;

		done = read(fd, data, size);
		if (done < 0 && errno == EINTR)
			continue;
		if (done <= 0)
			return false;
		data += done;
		size -= done;
	}

	return true;
}

void _delete_arr(void *p, unsigned len, unsigned off, unsigned num, size_t s)
{
	assert(off + num <= len);
	memmove(p + off*s, p + (off+num)*s, (len - (off+num))*s);
}
	
bool is_dir(const char *dirname)
{
	struct stat st;

	if (stat(dirname, &st) != 0)
		return false;

	if (!S_ISDIR(st.st_mode)) {
		errno = ENOTDIR;
		return false;
	}
	return true;
}
