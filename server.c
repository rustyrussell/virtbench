/* The framework for coordinating and timing benchmarks. */
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdarg.h>
#include <err.h>
#include <sys/wait.h>
#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include "talloc.h"
#include "stdrusty.h"
#include "benchmarks.h"

#define NUM_MACHINES 8
#define MAX_TEST_TIME ((u64)20)

static void __attribute__((noreturn)) usage(void)
{
	errx(1, "Usage: virtbench <virt-type> [benchmark]\n");
}

static const char *virtdir;

static const char *do_send_message(u32 dst, const char *str, unsigned int len,
				   int msecs, u64 *time)
{
	struct sockaddr_in saddr;
	struct timeval start, end;
	struct itimerval ival;
	u32 reply;
	int sock;

	sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
		return "creating socket";

	saddr.sin_family = AF_INET;
	saddr.sin_port = 6099;
	saddr.sin_addr.s_addr = htonl(clientip(dst));
	if (connect(sock, (struct sockaddr *)&saddr, sizeof(saddr)) != 0)
		return "connecting socket";

	ival.it_value.tv_sec = msecs / 1000;
	ival.it_value.tv_usec = (msecs % 1000) * 1000;
	ival.it_interval.tv_sec = ival.it_interval.tv_usec = 0;

	setitimer(ITIMER_REAL, &ival, NULL);
	gettimeofday(&start, NULL);
	if (send(sock, str, len, 0) != len) {
		alarm(0);
		return "sending on socket";
	}

	if (recv(sock, &reply, sizeof(reply), 0) != sizeof(reply)) {
		alarm(0);
		return "receiving on socket";
	}
	gettimeofday(&end, NULL);
	close(sock);

	ival.it_value.tv_sec = ival.it_value.tv_usec = 0;
	setitimer(ITIMER_REAL, &ival, NULL);

	if (reply != 0) {
		errno = 0;
		return "running benchmark";
	}

	*time = (end.tv_sec - start.tv_sec) * (u64)1000000000 
		+ (end.tv_usec - start.tv_usec) * (u64)1000;
	return NULL;
}

static u64 send_message(u32 dst, const char *benchmark, u32 runs)
{
	const char *errstr;
	char str[sizeof(runs) + strlen(benchmark) + 1];
	u64 time;

	memcpy(str, &runs, sizeof(runs));
	strcpy(str + sizeof(runs), benchmark);

	errstr = do_send_message(dst, str, sizeof(str), MAX_TEST_TIME*1000,
				 &time);
	if (errstr)
		err(1, errstr, benchmark);
	return time;
}

/* We don't want timeout to kill us, just abort recv. */
static void wakeup(int signo)
{
}

static bool ping_client(u32 dst)
{
	char str[] = "\0\0\0\0ping";
	u64 time;

	return do_send_message(dst, str, sizeof(str), 50, &time) == NULL;
}

/* Simple routine to suck a FILE * dry. */
static char *talloc_grab_file(const void *ctx, FILE *file)
{
	unsigned int off = 0, len = 10;
	char *ret;

	ret = talloc_array(ctx, char, len);
	ret[0] = '\0';
	while (fgets(ret + off, len - off, file)) {
		off += strlen(ret + off);
		if (off == len)
			ret = talloc_realloc(ctx, ret, char, len += 2);
	}
	return ret;
}

static int destroy_machine(char *name)
{
	char *cmd;
	FILE *f;
	int status;

	cmd = talloc_asprintf(NULL, "%s/stop_machine %s", virtdir, name);
	f = popen(cmd, "r");
	if (!f) {
		warn("Could not popen '%s'", cmd);
		return 0;
	}
	status = fclose(f);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		warnx("'%s' failed", cmd);
	talloc_free(cmd);
	return 0;
}

#define HIPQUAD(ip)				\
	((u8)(ip >> 24)),			\
	((u8)(ip >> 16)),			\
	((u8)(ip >> 8)),			\
	((u8)(ip))

static char **bringup_machines(void)
{
	unsigned int i, runs;
	char **names;
	bool up[NUM_MACHINES] = { false };
	bool some_down;

	names = talloc_array(talloc_autofree_context(), char *, NUM_MACHINES);

	printf("Bringing up machines"); fflush(stdout);
	for (i = 0; i < NUM_MACHINES; i++) {
		FILE *f;
		int status;
		char *cmd;

		cmd = talloc_asprintf(names, "%s/start_machine %i %i.%i.%i.%i",
				      virtdir, i, HIPQUAD(clientip(i)));
		f = popen(cmd, "r");
		if (!f)
			err(1, "Could not popen '%s'", cmd);
		names[i] = talloc_grab_file(names, f);
		status = fclose(f);
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
			errx(1, "'%s' failed", cmd);
		if (streq(names[i], ""))
			errx(1, "'%s' did not give a name", cmd);
		talloc_set_destructor(names[i], destroy_machine);
	}

	for (runs = 0; runs < 300; runs++) {
		some_down = false;
		for (i = 0; i < NUM_MACHINES; i++) {
			if (!up[i]) {
				up[i] = ping_client(i);
				if (!up[i])
					some_down = true;
				else {
					printf(".");
					fflush(stdout);
				}
			}
		}
		if (!some_down)
			break;
	}
	printf("\n");
	if (some_down)
		errx(1, "Not all machines came up");

	return names;
}

#define MAX_RESULTS 64
static unsigned pick_outlier(const u64 times[MAX_RESULTS])
{
	u64 avg = 0, worstres = 0;
	unsigned int i, worst;

	for (i = 0; i < MAX_RESULTS; i++)
		avg += times[i];
	avg /= MAX_RESULTS;

	worst = MAX_RESULTS;
	for (i = 0; i < MAX_RESULTS; i++) {
		u64 diff;
		if (avg > times[i])
			diff = avg - times[i];
		else
			/* Favour discarding high ones. */
			diff = (times[i] - avg) * 2;;
		/* More than 10% == outlier. */
		if (diff * 10 > avg && diff > worstres) {
			worst = i;
			worstres = diff;
		}
	}

	return worst;
}

/* Try to get a stable result. */
static u64 stable(unsigned int dst, struct benchmark *bench, unsigned int runs,
		  u64 *best)
{
	u64 times[MAX_RESULTS], res;
	unsigned int i, bad, oldbad = MAX_RESULTS;

	for (i = 0; i < MAX_RESULTS; i++) {
		send_message(dst, bench->name, runs);
		times[i] = send_message(dst, "", runs);
		if (times[i] < *best)
			*best = times[i];
	}

	while ((bad = pick_outlier(times)) != MAX_RESULTS) {
		if (bad == oldbad)
			bad = random() % MAX_RESULTS;
		oldbad = bad;
		send_message(dst, bench->name, runs);
		times[bad] = send_message(dst, "", runs);
		if (times[bad] < *best)
			*best = times[bad];
	}

	res = 0;
	for (i = 0; i < MAX_RESULTS; i++)
		res += times[i];

	return res / MAX_RESULTS;
}

static u64 single_bench(unsigned int dst, struct benchmark *bench)
{
	unsigned int i;
	u64 best = -1ULL;

	/* We use "best" as a guestimate of the overhead. */
	stable(dst, bench, 0, &best);
	for (i = 4; i < 64; i++) {
		u64 time = stable(dst, bench, (u64)1 << i, &best);
		if (time > best * 256)
			return (time - best) / (1 << i);
	}
	assert(0);
}

u64 do_single_bench(struct benchmark *bench)
{
	return single_bench(0, bench);
}

int main(int argc, char *argv[])
{
	struct benchmark *b;
	char **names;
	struct sigaction act;
	bool done = false;

	if (argc < 2 || argc > 3)
		usage();

	act.sa_handler = wakeup;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGALRM, &act, NULL);

	virtdir = argv[1];
	if (!is_dir(virtdir))
		usage();

	names = bringup_machines();

	for (b = __start_benchmarks; b < __stop_benchmarks; b++) {
		u64 result;
		if (argv[2] && !streq(b->name, argv[2]))
			continue;
		printf("Running benchmark '%s'...", b->name);
		fflush(stdout);
		result = b->local(b);
		printf(b->format, (unsigned int)result);
		printf("\n");
		done = true;
	}

	if (!done)
		usage();
	return 0;
}

/* Dummy for compiling benchmarks. */
bool wait_for_start(int sock)
{
	assert(0);
}
void send_ack(int sock, struct sockaddr *from, socklen_t *fromlen)
{
	assert(0);
}
