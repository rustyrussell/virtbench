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
	fprintf(stderr, "Usage: virtbench [--ifname=<interface>][--profile][--progress][--cvs=<file>] <virt-type> [benchmark]\n");

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
	ival.it_interval.tv_sec = ival.it_interval.tv_usec = 0;
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

static struct sockaddr_in get_server_addr(int sock, const char *ifname)
{
	struct ifreq ifr;
	struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;
	struct sockaddr_in saddr;
	socklen_t socklen = sizeof(saddr);

	strcpy(ifr.ifr_name, ifname);
	sin->sin_family = AF_INET;
	if (ioctl(sock, SIOCGIFADDR, &ifr) != 0)
		err(1, "Getting interface address for %s", ifname);

	if (getsockname(sock, (struct sockaddr *)&saddr, &socklen) != 0)
		err(1, "getting socket name");
	saddr.sin_addr = sin->sin_addr;
	return saddr;
}

static char **bringup_machines(int sock, const char *ifname)
{
	unsigned int i, done;
	char **names;
	char *startcmd;
	struct sockaddr_in addr;
	
	startcmd = talloc_asprintf(NULL, "%s/start", virtdir);
	if (!do_command(startcmd))
		errx(1, "Start command failed for %s", virtdir);
	(void)talloc_steal(talloc_autofree_context(), startcmd);
	talloc_set_destructor(startcmd, stop);

	names = talloc_array(talloc_autofree_context(), char *, NUM_MACHINES);

	addr = get_server_addr(sock, ifname);

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
		/* Damn spurious gcc warnings. */
		(void)talloc_reference(names[i], startcmd);
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

static void reset_profile(void)
{
	system("readprofile -r");
}

static void dump_profile(void)
{
	system("readprofile");
}

struct results *do_single_bench(struct benchmark *bench, bool rough,
				unsigned int forced_runs)
{
	unsigned int runs = forced_runs, prev_runs = -1U;
	struct results *r = new_results();
	int client[1] = { random() % NUM_MACHINES };

	do {
		struct timeval start;
		if (runs != prev_runs) {
			if (progress) {
				printf("%u runs:", runs);
				fflush(stdout);
			}
			if (profile)
				reset_profile();
			prev_runs = runs;
		}
		setup_bench(client[0], bench->name, "", 0, runs);
		start_timer(&start);
		send_start_to_client(client[0]);
		add_result(r, end_test(&start, client, 1));
		if (progress) {
			printf(".");
			fflush(stdout);
		}
	} while (!results_done(r, &runs, rough, forced_runs));
	if (profile)
		dump_profile();
	return r;
}

static u32 getip(int client)
{
	struct sockaddr_in saddr;
	socklen_t len = sizeof(saddr);

	if (getpeername(sockets[client], (struct sockaddr *)&saddr, &len) != 0)
		err(1, "getting peer name for client %i", client);

	return ntohl(saddr.sin_addr.s_addr);
}

static struct results *some_pair_bench(struct benchmark *bench,
				       bool onestop, bool rough,
				       unsigned int forced_runs)
{
	unsigned int runs = forced_runs, prev_runs = 1;
	struct results *r = new_results();
	int clients[2];

	clients[0] = (random() % NUM_MACHINES);
	do {
		clients[1] = (random() % NUM_MACHINES);
	} while (clients[1] == clients[0]);

	do {
		struct timeval start;
		struct pair_opt opt;

		if (runs != prev_runs) {
			if (progress) {
				printf("%u runs:", runs);
				fflush(stdout);
			}
			if (profile)
				reset_profile();
			prev_runs = runs;
		}

		opt.yourip = getip(clients[0]);
		opt.otherip = getip(clients[1]);
		opt.start = 1;
		setup_bench(clients[0], bench->name, &opt, sizeof(opt), runs);
		opt.yourip = getip(clients[1]);
		opt.otherip = getip(clients[0]);
		opt.start = 0;
		setup_bench(clients[1], bench->name, &opt, sizeof(opt), runs);

		start_timer(&start);
		send_start_to_client(clients[0]);
		send_start_to_client(clients[1]);
		add_result(r, end_test(&start, clients, onestop ? 1 : 2));
		if (progress) {
			printf(".");
			fflush(stdout);
		}
	} while (!results_done(r, &runs, rough, forced_runs));
	return r;
}

struct results *do_pair_bench(struct benchmark *bench, bool rough,
			      unsigned int forced_runs)
{
	return some_pair_bench(bench, false, rough, forced_runs);
}

struct results *do_pair_bench_onestop(struct benchmark *bench, bool rough,
			      unsigned int forced_runs)
{
	return some_pair_bench(bench, true, rough, forced_runs);
}

#define MB * 1024 * 1024

static void receive_data(int fd, void *mem, unsigned long size)
{
	long ret, done = 0;
	while ((ret = read(fd, mem+done, size-done)) > 0) {
		done += ret;
		if (done == size)
			return;
	}
	if (ret < 0 || size != 0)
		err(1, "reading @%lu %lu from other end %li", done, size, ret);
}

struct results *do_receive_bench(struct benchmark *bench, bool rough,
				 unsigned int forced_runs)
{
	unsigned int runs = forced_runs, prev_runs = -1U;
	struct results *r = new_results();
	int client[1] = { random() % NUM_MACHINES };
	char *recvmem = talloc_array(r, char, NET_BANDWIDTH_SIZE);

	do {
		struct timeval start;
		unsigned int i;

		if (runs != prev_runs) {
			if (progress) {
				printf("%u runs:", runs);
				fflush(stdout);
			}
			if (profile)
				reset_profile();
			prev_runs = runs;
		}
		setup_bench(client[0], bench->name, "", 0, runs);
		start_timer(&start);
		send_start_to_client(client[0]);

		/* Read warmup */
		receive_data(sockets[client[0]], recvmem, NET_WARMUP_BYTES);

		/* Read real results. */
		for (i = 0; i < runs; i++)
			receive_data(sockets[client[0]], recvmem,
				     NET_BANDWIDTH_SIZE);

		add_result(r, end_test(&start, client, 1));
		if (progress) {
			printf(".");
			fflush(stdout);
		}
	} while (!results_done(r, &runs, rough, forced_runs));
	if (profile)
		dump_profile();
	return r;
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
	unsigned int forced_runs = 0;
	bool done = false, rough = false;
	const char *ifname = "eth0";
	struct option lopts[] = {
		{ "progress", 0, 0, 'p' },
		{ "profile", 0, 0, 'P' },
		{ "csv", 1, 0, 'c' },
		{ "help", 0, 0, 'h' },
		{ "ifname", 1, 0, 'i' },
		{ "distribution", 0, 0, 'd' },
		{ "rough", 0, 0, 'r' },
		{ "runs", 1, 0, 'R' },
		{ 0 },
	};
	const char *sopts = "phc:";
	int ch, opt_ind;
	const char *csv_file = 0;
	FILE *csv_fp = NULL;
	char *(*printer)(struct results *r) = results_to_quick_summary;

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
		case 'i':
			ifname = optarg;
			break;
		case 'd':
			printer = results_to_dist_summary;
			break;
		case 'r':
			rough = true;
			break;
		case 'R':
			forced_runs = atoi(optarg);
			break;
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

	names = bringup_machines(sock, ifname);

	for (b = __start_benchmarks; b < __stop_benchmarks; b++) {
		struct results *results;
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
		results = b->server(b, rough, forced_runs);
		if (progress)
			printf("\n");

		if (csv_fp)
			fprintf(csv_fp, "%s\n", results_to_csv(results));

		if (forced_runs)
			printf("%s (x %u): %s\n",
			       b->pretty_name, forced_runs, printer(results));
		else
			printf("%s: %s\n", b->pretty_name, printer(results));
	}

	if (!done)
		usage(1);

	if (csv_fp)
		fclose(csv_fp);

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
char *blockdev;
