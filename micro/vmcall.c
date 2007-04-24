#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include "../benchmarks.h"

#ifdef __i386__
#define VMCALL_NOP 0
#define VMMCALL_NOP 0

static sig_atomic_t bad_instruction;

static void illegal_instruction(int sig)
{
	bad_instruction = 1;
	_exit(1);
}

static int vmmcall(unsigned int cmd)
{
	signal(SIGILL, illegal_instruction);
	asm volatile(".byte 0x0F,0x01,0xD9\n" ::"a"(cmd));
	signal(SIGILL, SIG_DFL);

	return 0;
}

static int vmcall(unsigned int cmd)
{
	signal(SIGILL, illegal_instruction);
	asm volatile(".byte 0x0F,0x01,0xC1\n" ::"a"(cmd));
	signal(SIGILL, SIG_DFL);

	return 0;
}

static int try_vmcall(int intel)
{
	pid_t pid;
	int status;

	pid = fork();
	if (pid == 0) {
		if (intel)
			vmcall(VMCALL_NOP);
		else
			vmmcall(VMMCALL_NOP);
		exit(0);
	}

	if (waitpid(pid, &status, 0) == -1)
		return -1;

	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		return 0;

	return -1;
}

static void do_vmcall(int fd, u32 runs,
		      struct benchmark *bench, const void *opts)
{
	int intel = streq(bench->name, "vmcall");

	send_ack(fd);

	if (wait_for_start(fd)) {
		u32 i;

		for (i = 0; i < runs; i++) {
			if (intel)
				vmcall(VMCALL_NOP);
			else
				vmmcall(VMMCALL_NOP);
		}
		
		send_ack(fd);
	}
}

static const char *vmcall_should_not_run(const char *virtdir, struct benchmark *b)
{
	if (streq(b->name, "vmcall")) {
		if (try_vmcall(1))
			return "not a VT guest";
	} else {
		if (try_vmcall(0))
			return "not an SVM guest";
	}
	return NULL;
}

struct benchmark vmcall_wait_benchmark _benchmark_
= { "vmcall", "Time for one VMCALL (VT) instruction",
    do_single_bench, do_vmcall, vmcall_should_not_run };

struct benchmark vmmcall_wait_benchmark _benchmark_
= { "vmmcall", "Time for one VMMCALL (SVM) instruction",
    do_single_bench, do_vmcall, vmcall_should_not_run };
#endif /* __i386__ */
