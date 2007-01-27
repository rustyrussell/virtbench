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
#include <sys/ioctl.h>
#include <getopt.h>
#include <net/if.h>
#include "talloc.h"
#include "stdrusty.h"
#include "benchmarks.h"

#define MAX_TEST_TIME ((u64)20)

static void __attribute__((noreturn)) usage(int exitstatus)
{
	struct benchmark *b;
	fprintf(stderr, "Usage: virtbench [--profile][--progress][--cvs=<file>] <virt-type> [benchmark]\n");

	printf("Benchmarks are:\n");
	for (b = __start_benchmarks; b < __stop_benchmarks; b++)
		printf("  %s\n", b->name);
	exit(exitstatus);
}

static const char *virtdir;
static int sockets[NUM_MACHINES] = { [0 ... NUM_MACHINES-1] = -1 };
static bool progress = false, profile = false;

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
	return write(sockets[dst], buf, len) == len;
}

static void send_start_to_client(int dst)
{
	const char str[] = "\0\0\0\0\0";

	if (!send_to_client(dst, str, sizeof(str)))
		err(1, "sending start to %i", dst);
}

static void recv_from_client(int dst)
{
	int ans;
	switch (read(sockets[dst], &ans, sizeof(ans))) {
	case sizeof(ans):
		return;
	case -1:
		if (errno == EINTR)
			errno = ETIMEDOUT;
		err(1, "reading reply from client %i", dst);
	case 0:
		errx(1, "client %i closed connection", dst);
	default:
		err(1, "strange read reply from client %i", dst);
	}
}

#define HIPQUAD(ip)				\
	((u8)(ip >> 24)),			\
	((u8)(ip >> 16)),			\
	((u8)(ip >> 8)),			\
	((u8)(ip))

static int set_fds(fd_set *fds, const int clients[], unsigned num)
{
	int i, max_fd = 0;

	FD_ZERO(fds);
	for (i = 0; i < num; i++) {
		FD_SET(sockets[clients[i]], fds);
		max_fd = max(max_fd, sockets[clients[i]]);
	}
	return max_fd;
}

static u64 end_test(const struct timeval *start,
		    const int clients[], unsigned num)
{
	unsigned int i, num_done;
	struct timeval end, timeout;
	fd_set orig_rfds, rfds;
	int max_fd;

	max_fd = set_fds(&orig_rfds, clients, num);
	rfds = orig_rfds;

	timeout.tv_sec = MAX_TEST_TIME;
	timeout.tv_usec = 0;
	num_done = 0;
	while (select(max_fd+1, &rfds, NULL, NULL, &timeout)) {
		for (i = 0; i < num; i++) {
			if (FD_ISSET(sockets[clients[i]], &rfds)) {
				recv_from_client(clients[i]);
				num_done++;
				if (num_done == num) {
					gettimeofday(&end, NULL);
					return (end.tv_sec - start->tv_sec)
						* (u64)1000000000 
						+ (end.tv_usec-start->tv_usec)
						* (u64)1000;
				}
				FD_CLR(sockets[clients[i]], &orig_rfds);
			}
		}
		rfds = orig_rfds;
	}

	for (i = 0; i < num; i++) {
		if (FD_ISSET(sockets[clients[i]], &orig_rfds))
			warnx("no reply from client %i", clients[i]);
	}
	exit(1);
}

/* We don't want timeout to kill us, just abort recv. */
static void wakeup(int signo)
{
}

static void setup_bench(u32 dst, const char *benchname, const void *opts,
			int optlen, int runs)
{
	char str[sizeof(runs) + strlen(benchname) + 1 + optlen];

	memcpy(str, &runs, sizeof(runs));
	strcpy(str + sizeof(runs), benchname);
	memcpy(str + sizeof(runs) + strlen(benchname)+1, opts, optlen);

	start_timeout_timer(5000);
	if (!send_to_client(dst, str, sizeof(str)))
		err(1, "sending setup for %s to client %i", benchname, dst);

	recv_from_client(dst);
	stop_timeout_timer();
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
		if (off == len - 1)
			ret = talloc_realloc(ctx, ret, char, len *= 2);
	}
	return ret;
}

static int do_command(const char *cmd)
{
	FILE *f;
	int status;

	f = popen(cmd, "r");
	if (!f)
		return 0;

	status = fclose(f);
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int destroy_machine(char *name)
{
	char *cmd = talloc_asprintf(NULL, "%s/stop_machine %s", virtdir, name);
	if (!do_command(cmd))
		warnx("'%s' failed", cmd);
	talloc_free(cmd);
	return 0;
}

static int stop(char *unused)
{
	char *cmd = talloc_asprintf(NULL, "%s/stop", virtdir);
	if (!do_command(cmd))
		warnx("'%s' failed", cmd);
	talloc_free(cmd);
	return 0;
}

static struct sockaddr_in get_server_addr(int sock)
{
	struct ifreq ifr;
	struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;
	struct sockaddr_in saddr;
	socklen_t socklen = sizeof(saddr);

	/* This assumes we have an eth0. */
	strcpy(ifr.ifr_name, "eth0");
	sin->sin_family = AF_INET;
	if (ioctl(sock, SIOCGIFADDR, &ifr) != 0)
		err(1, "Getting interface address for eth0");

	if (getsockname(sock, (struct sockaddr *)&saddr, &socklen) != 0)
		err(1, "getting socket name");
	saddr.sin_addr = sin->sin_addr;
	return saddr;
}

static char **bringup_machines(int sock)
{
	unsigned int i, done;
	char **names;
	char *startcmd;
	struct sockaddr_in addr;

	startcmd = talloc_asprintf(NULL, "%s/start", virtdir);
	do_command(startcmd);
	(void)talloc_steal(talloc_autofree_context(), startcmd);
	talloc_set_destructor(startcmd, stop);

	names = talloc_array(talloc_autofree_context(), char *, NUM_MACHINES);

	addr = get_server_addr(sock);

	printf("Bringing up machines"); fflush(stdout);
	for (i = 0; i < NUM_MACHINES; i++) {
		FILE *f;
		int status;
		char *cmd;

		cmd = talloc_asprintf(names, "%s/start_machine %i %i.%i.%i.%i %i",
				      virtdir, i,
				      HIPQUAD(ntohl(addr.sin_addr.s_addr)),
				      ntohs(addr.sin_port));
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
		talloc_reference(names[i], startcmd);
	}

	for (done = 0; done < NUM_MACHINES; done++) {
		int clientid, fd;

		start_timeout_timer(30000);
		fd = accept(sock, NULL, NULL);
		if (fd < 0)
			err(1, "accepting connection from client");
		stop_timeout_timer();
		printf("."); fflush(stdout);
		if (read(fd, &clientid, sizeof(clientid)) != sizeof(clientid))
			err(1, "reading id from client");
		sockets[clientid] = fd;
	}
	printf("\n");

	return names;
}

#define MAX_RESULTS 64
static void pick_outliers(const u64 times[MAX_RESULTS],
			  bool outliers[MAX_RESULTS])
{
	unsigned int best_neighbours = 0, best = 0;
	unsigned int i;

	for (i = 0; i < MAX_RESULTS; i++) {
		unsigned int j, neighbours = 0;
		for (j = 0; j < MAX_RESULTS; j++) {
			u64 diff;
			if (times[j] > times[i])
				diff = times[j] - times[i];
			else
				/* Favour discarding high ones. */
				diff = (times[i] - times[j]) * 2;
			if (diff * 10 <= times[i])
				neighbours++;
		}
		if (neighbours > best_neighbours) {
			best_neighbours = neighbours;
			best = i;
		}
	}

	for (i = 0; i < MAX_RESULTS; i++) {
		u64 diff;
		if (times[best] > times[i])
			diff = times[best] - times[i];
		else
			/* Favour discarding high ones. */
			diff = (times[i] - times[best]) * 2;

		/* More than 10% == outlier. */
		outliers[i] = (diff * 10 > times[best]);
		if (progress && outliers[i])
			printf("%c", times[i] > times[best] ? '>' : '<');
	}
}

/* Try to get a stable result. */
static int next_place(int prev,
		      const u64 times[MAX_RESULTS],
		      bool outliers[MAX_RESULTS])

{
	if (prev == MAX_RESULTS-1) {
		pick_outliers(times, outliers);
		prev = -1;
	}
	for (prev++; prev < MAX_RESULTS; prev++)
		if (outliers[prev])
			return prev;
	return -1;
}

static u64 average(u64 times[MAX_RESULTS])
{
	u64 i, total = 0;

	for (i = 0; i < MAX_RESULTS; i++)
		total += times[i];

	return total / MAX_RESULTS;
}

static void reset_profile(void)
{
	system("readprofile -r");
}

static void dump_profile(void)
{
	system("readprofile");
}

u64 do_single_bench(struct benchmark *bench)
{
	int slot;
	u64 runs = 0, best = -1ULL;
	u64 times[MAX_RESULTS];
	int client[1] = { random() % NUM_MACHINES };
	bool outliers[MAX_RESULTS];

	for (;;) {
		memset(outliers, 0xFF, sizeof(outliers));
		slot = -1;
		if (progress)
			printf("%llu runs:", runs);
		if (profile)
			reset_profile();
		while ((slot = next_place(slot, times, outliers)) != -1) {
			struct timeval start;
			setup_bench(client[0], bench->name, "", 0, runs);
			start_timer(&start);
			send_start_to_client(client[0]);
			times[slot] = end_test(&start, client, 1);
			if (progress) {
				printf(".");
				fflush(stdout);
			}
			if (times[slot] < best)
				best = times[slot];

			/* If we do MAX_RESULTS and average is less
			 * than 256 times the best result, we need to
			 * increase number of runs. */
			if (slot == MAX_RESULTS-1
			    && average(times) < best * 256)
				break;
		}

		if (!runs)
			runs = 1;
		else if (slot == -1) {
			if (profile)
				dump_profile();
			return (average(times) - best) / runs;
		} else {
			u64 avg = (average(times) - best) / runs;

			/* Jump to approx how many we'd need... */
			do {
				runs <<= 1;
			} while (runs * avg < 256 * best);
		}
	}
	assert(0);
}

static u32 getip(int client)
{
	struct sockaddr_in saddr;
	socklen_t len = sizeof(saddr);

	if (getpeername(sockets[client], (struct sockaddr *)&saddr, &len) != 0)
		err(1, "getting peer name for client %i", client);

	return ntohl(saddr.sin_addr.s_addr);
}

static u64 some_pair_bench(struct benchmark *bench, bool onestop)
{
	int slot;
	u64 runs = 0, best = -1ULL;
	u64 times[MAX_RESULTS];
	int clients[2];
	bool outliers[MAX_RESULTS];

	clients[0] = (random() % NUM_MACHINES);
	do {
		clients[1] = (random() % NUM_MACHINES);
	} while (clients[1] == clients[0]);

	for (;;) {
		slot = -1;
		memset(outliers, 0xFF, sizeof(outliers));
		if (progress)
			printf("%llu runs:", runs);
		if (profile)
			reset_profile();
		while ((slot = next_place(slot, times, outliers)) != -1) {
			struct timeval start;
			struct pair_opt opt;

			opt.yourip = getip(clients[0]);
			opt.otherip = getip(clients[1]);
			opt.start = 1;
			setup_bench(clients[0], bench->name, &opt, sizeof(opt),
				    runs);
			opt.yourip = getip(clients[1]);
			opt.otherip = getip(clients[0]);
			opt.start = 0;
			setup_bench(clients[1], bench->name, &opt, sizeof(opt),
				    runs);

			start_timer(&start);
			send_start_to_client(clients[0]);
			send_start_to_client(clients[1]);
			times[slot] = end_test(&start, clients, onestop ? 1 : 2);
			if (progress) {
				printf(".");
				fflush(stdout);
			}
			if (times[slot] < best)
				best = times[slot];

			/* If we do MAX_RESULTS and average is less
			 * than 256 times the best result, we need to
			 * increase number of runs. */
			if (slot == MAX_RESULTS-1
			    && average(times) < best * 256)
				break;
		}
		if (!runs)
			runs = 1;
		else if (slot == -1) {
			if (profile)
				dump_profile();
			return (average(times) - best) / runs;
		} else {
			u64 avg = (average(times) - best) / runs;

			/* Jump to approx how many we'd need... */
			do {
				runs <<= 1;
			} while (runs * avg < 256 * best);
		}
	}
	assert(0);
}

u64 do_pair_bench(struct benchmark *bench)
{
	return some_pair_bench(bench, false);
}

u64 do_pair_bench_onestop(struct benchmark *bench)
{
	return some_pair_bench(bench, true);
}

static bool benchmark_listed(const char *bench, char *argv[])
{
	unsigned int i;

	/* No args means "all" */
	if (!argv[0])
		return true;

	for (i = 0; argv[i]; i++)
		if (streq(bench, argv[i]))
			return true;
	return false;
}

int main(int argc, char *argv[])
{
	struct benchmark *b;
	char **names;
	struct sigaction act;
	int sock;
	bool done = false;
	struct option lopts[] = {
		{ "progress", 0, 0, 'p' },
		{ "profile", 0, 0, 'P' },
		{ "csv", 1, 0, 'c' },
		{ "help", 0, 0, 'h' },
		{ 0 },
	};
	const char *sopts = "phc:";
	int ch, opt_ind;
	const char *csv_file = 0;
	FILE *csv_fp = NULL;

	while ((ch = getopt_long(argc, argv, sopts, lopts, &opt_ind)) != -1) {
		switch (ch) {
		case 'p':
			progress = true;
			break;
		case 'P':
			profile = true;
			break;
		case 'c':
			csv_file = optarg;
			break;
		case 'h':
			usage(0);
		default:
			usage(1);
			break;
		}
	}

	if (argc - optind < 1)
		usage(1);

	if (csv_file) {
		csv_fp = fopen(csv_file, "a");
		if (csv_fp == NULL)
			err(errno, "opening CSV file '%s'", csv_file);
	}

	act.sa_handler = wakeup;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGALRM, &act, NULL);

	virtdir = argv[optind];
	if (!is_dir(virtdir))
		usage(1);

	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		err(1, "creating socket");
	if (listen(sock, 0) != 0)
		err(1, "listening on socket");

	names = bringup_machines(sock);

	for (b = __start_benchmarks; b < __stop_benchmarks; b++) {
		u64 result;
		const char *reason;
		if (!benchmark_listed(b->name, argv+optind+1))
			continue;
		done = true;

		if (b->should_not_run
		    && (reason = b->should_not_run(virtdir, b)) != NULL) {
			printf("DISABLED %s: %s\n", b->name, reason);
			continue;
		}
		if (progress) {
			printf("Running benchmark %s", b->name);
			fflush(stdout);
		}
		result = b->server(b);
		if (progress)
			printf("\n");

		if (csv_fp) {
			if (b != __start_benchmarks)
				fprintf(csv_fp, ", ");
			fprintf(csv_fp, "%Lu", result);
		}

		printf(b->format, (unsigned int)result);
		printf("\n");
	}

	if (!done)
		usage(1);

	if (csv_fp) {
		fprintf(csv_fp, "\n");
		fclose(csv_fp);
	}

	return 0;
}

/* Dummy for compiling benchmarks. */
bool wait_for_start(int sock)
{
	assert(0);
}
void send_ack(int sock)
{
	assert(0);
}
char *argv0;
