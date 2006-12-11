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

static void __attribute__((noreturn)) usage(bool showbench)
{
	fprintf(stderr, "Usage: virtbench <virt-type> [benchmark]\n");
	if (showbench) {
		struct benchmark *b;

		printf("Benchmarks are:\n");
		for (b = __start_benchmarks; b < __stop_benchmarks; b++)
			printf("  %s\n", b->name);
	}
	exit(0);
}

static const char *virtdir;
static int udpsock;

static void start_timeout_timer(unsigned int msecs)
{
	struct itimerval ival;
	ival.it_value.tv_sec = msecs / 1000;
	ival.it_value.tv_usec = (msecs % 1000) * 1000;
	ival.it_interval.tv_sec = ival.it_interval.tv_usec = 0;
	setitimer(ITIMER_REAL, &ival, NULL);
}

static void stop_timeout_timer(void)
{
	struct itimerval ival;
	int olderr = errno;

	ival.it_value.tv_sec = ival.it_value.tv_usec = 0;
	setitimer(ITIMER_REAL, &ival, NULL);

	errno = olderr;
}

static void start_timer(struct timeval *start)
{
	start_timeout_timer(MAX_TEST_TIME * 1000);
	gettimeofday(start, NULL);
}

static bool send_to_client(int dst, const void *buf, unsigned int len)
{
	struct sockaddr_in saddr;

	saddr.sin_family = AF_INET;
	saddr.sin_port = 6099;
	saddr.sin_addr.s_addr = htonl(clientip(dst));

	return sendto(udpsock, buf, len, 0, (void *)&saddr, sizeof(saddr))
		== len;
}

static void send_start_to_client(int dst)
{
	const char str[] = "\0\0\0\0\0";

	if (!send_to_client(dst, str, sizeof(str)))
		err(1, "sending start to %i", dst);
}

static bool recv_from_client(void)
{
	int ans;

	if (recv(udpsock, &ans, sizeof(ans), 0) != sizeof(ans)) {
		if (errno == EINTR)
			errno = ETIMEDOUT;
		return false;
	}
	return ans == 0;
}

static u64 end_test(const struct timeval *start, int clients)
{
	int i;
	struct timeval end;

	for (i = 0; i < clients; i++)
		if (!recv_from_client())
			err(1, "receiving reply");

	gettimeofday(&end, NULL);
	stop_timeout_timer();

	return (end.tv_sec - start->tv_sec) * (u64)1000000000 
		+ (end.tv_usec - start->tv_usec) * (u64)1000;
}

/* We don't want timeout to kill us, just abort recv. */
static void wakeup(int signo)
{
}

static bool ping_client(u32 dst)
{
	char str[] = "\0\0\0\0ping";
	bool ok;

	if (!send_to_client(dst, str, sizeof(str)))
		return false;

	start_timeout_timer(50);
	ok = recv_from_client();
	stop_timeout_timer();
	return ok;
}

static void setup_bench(u32 dst, const char *benchname, const void *opts,
			int optlen, int runs)
{
	bool ok;
	char str[sizeof(runs) + strlen(benchname) + 1 + optlen];

	memcpy(str, &runs, sizeof(runs));
	strcpy(str + sizeof(runs), benchname);
	memcpy(str + sizeof(runs) + strlen(benchname)+1, opts, optlen);

	if (!send_to_client(dst, str, sizeof(str)))
		err(1, "sending setup for %s to client %i", benchname, dst);

	start_timeout_timer(1000);
	ok = recv_from_client();
	stop_timeout_timer();
	if (!ok)
		err(1, "client %i acking setup for %s", dst, benchname);
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
static int next_place(u64 times[MAX_RESULTS], int *data)
{
	unsigned int bad;

	/* First we fill in all the results. */
	if (*data < MAX_RESULTS)
		return (*data)++;

	bad = pick_outlier(times);
	if (bad == MAX_RESULTS)
		return -1;

	/* Avoid replacing the same one twice. */
	if (bad + MAX_RESULTS + 1 == *data)
		bad = random() % MAX_RESULTS;

	*data = bad + MAX_RESULTS + 1;
	return bad;
}

static u64 average(u64 times[MAX_RESULTS])
{
	u64 i, total = 0;

	for (i = 0; i < MAX_RESULTS; i++)
		total += times[i];

	return total / MAX_RESULTS;
}

u64 do_single_bench(struct benchmark *bench)
{
	int data, i, slot;
	u64 best = -1ULL;
	u64 times[MAX_RESULTS];
	unsigned int dst = random() % NUM_MACHINES;

	for (i = 0; i < 64; i++) {
		u64 avg;

		data = 0;
		while ((slot = next_place(times, &data)) != -1) {
			struct timeval start;
			setup_bench(dst, bench->name, "", 0, (u64)1<<i);
			start_timer(&start);
			send_start_to_client(dst);
			times[slot] = end_test(&start, 1);
			if (times[slot] < best)
				best = times[slot];
		}

		/* Was this the warmup? */
		if (i == 0) {
			i = 3;
			continue;
		}

		avg = average(times);
		/* We use "best" as a guestimate of the overhead. */
		if (avg > best * 256)
			return (avg - best) / (1 << i);
	}
	assert(0);
}

u64 do_pair_bench(struct benchmark *bench)
{
	int mach1, mach2;
	u32 ip1, ip2;
	int data, i, slot;
	u64 best = -1ULL;
	u64 times[MAX_RESULTS];

	mach1 = (random() % NUM_MACHINES);
	do {
		mach2 = (random() % NUM_MACHINES);
	} while (mach2 == mach1);

	ip1 = clientip(mach1);
	ip2 = clientip(mach2);

	for (i = 0; i < 64; i++) {
		u64 avg;

		data = 0;
		while ((slot = next_place(times, &data)) != -1) {
			struct timeval start;
			struct pair_opt opt;
			opt.yourip = ip1;
			opt.otherip = ip2;
			opt.start = 1;
			setup_bench(mach1, bench->name, &opt, sizeof(opt),
				    (u64)1<<i);
			opt.yourip = ip2;
			opt.otherip = ip1;
			opt.start = 0;
			setup_bench(mach2, bench->name, &opt, sizeof(opt),
				    (u64)1<<i);
			start_timer(&start);
			send_start_to_client(mach1);
			send_start_to_client(mach2);
			times[slot] = end_test(&start, 2);
			if (times[slot] < best)
				best = times[slot];
		}

		/* Was this the warmup? */
		if (i == 0) {
			i = 3;
			continue;
		}

		avg = average(times);
		/* We use "best" as a guestimate of the overhead. */
		if (avg > best * 256)
			return (avg - best) / (1 << i);
	}
	assert(0);
}

int main(int argc, char *argv[])
{
	struct benchmark *b;
	char **names;
	struct sigaction act;
	bool done = false;

	if (argc < 2 || argc > 3)
		usage(false);

	act.sa_handler = wakeup;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGALRM, &act, NULL);

	virtdir = argv[1];
	if (!is_dir(virtdir))
		usage(false);

	udpsock = socket(PF_INET, SOCK_DGRAM, 0);
	if (udpsock < 0)
		err(1, "creating udp socket");

	names = bringup_machines();

	for (b = __start_benchmarks; b < __stop_benchmarks; b++) {
		u64 result;
		if (argv[2] && !streq(b->name, argv[2]))
			continue;
		printf("Running benchmark '%s'...", b->name);
		fflush(stdout);
		result = b->server(b);
		printf(b->format, (unsigned int)result);
		printf("\n");
		done = true;
	}

	if (!done)
		usage(true);
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
