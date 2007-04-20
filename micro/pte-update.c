#define _GNU_SOURCE
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <err.h>
#include "../benchmarks.h"

#define PAGE_SIZE getpagesize()

#ifndef MREMAP_FIXED
#define MREMAP_FIXED 2
#endif

static void do_pte_update(int fd, u32 runs,
			  struct benchmark *bench, const void *opts)
{
	char *source, *dest;

	/* we claim two pages to ensure that we can use the vaddr space at
	   source + PAGE_SIZE. */

	source = mmap(NULL, 2 * PAGE_SIZE, PROT_READ | PROT_WRITE,
		      MAP_PRIVATE | MAP_LOCKED | MAP_ANONYMOUS | MAP_POPULATE,
		      -1, 0);
	if (source == MAP_FAILED)
		err(errno, "mmap");

	/* be extra sure that it's really mapped in */
	*source = 0;

	dest = source + PAGE_SIZE;

	send_ack(fd);

	/* this isn't terribly safe.  can we reserve this address space? */
	if (wait_for_start(fd)) {
		u32 i;

		for (i = 0; i < runs; i++) {
			char *tmp;

			/* the two PTE updates should be unset PTE_P on source
			   and setting up the new page on dest */
			dest = mremap(source, PAGE_SIZE, PAGE_SIZE,
				      MREMAP_MAYMOVE | MREMAP_FIXED, dest);
			if (dest == MAP_FAILED)
				err(errno, "mremap %d", i);

			/* be sure it's really in memory */
			*dest = 0;

			tmp = source;
			source = dest;
			dest = tmp;
		}

		send_ack(fd);
	}

	if (munmap(source, PAGE_SIZE) == -1)
		errx(errno, "munmap");
}

struct benchmark pte_update_wait_benchmark _benchmark_
= { "pte-update", "Time for two PTE updates", do_single_bench, do_pte_update };
