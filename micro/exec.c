#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <err.h>
#include "../benchmarks.h"

void exec_test(char *runstr)
{
	char *argv[] = { "./virtclient", runstr, NULL };
	extern char **environ;
	int i;

	for (i = 8; i >= 0; i--) {
		runstr[i]--;
		if (runstr[i] == '0' - 1)
			runstr[i] = '9';
		else
			break;
	}
	if (i == -1)
		exit(0);
	execve("./virtclient", argv, environ);
	exit(1);
}

static void do_syscall_exec(int fd, u32 runs,
			    struct benchmark *bench, const void *opts)
{
	char runstr[CHAR_SIZE(int)];
	int status;

	sprintf(runstr, "%09i", runs);

	switch (fork()) {
	case 0:
		send_ack(fd);

		if (wait_for_start(fd))
			exec_test(runstr);
		else
			exit(0);
	case -1:
		err(1, "forking");

	default:
		wait(&status);
		if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
			send_ack(fd);
		else
			errx(1, "child failed");
	}
}

struct benchmark exec_benchmark _benchmark_
= { "exec", "Time to exec client once: %u nsec",
    do_single_bench, do_syscall_exec };

