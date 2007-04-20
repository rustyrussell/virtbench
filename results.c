/* File to contain the result analysis. */
#include <stdarg.h>
#include <sys/types.h>
#include <stdio.h>
#include <assert.h>
#include "talloc.h"
#include "benchmarks.h"

#define MINIMUM_RUNS 10

struct results {
	unsigned int num_results;
	/* Fastest ever time (usually from num_runs = 0) */
	u64 overhead;
	u64 minimum, maximum;
	u64 *results;
	/* Once we're done, this contains the analysis. */
	struct peak *peaks;
	unsigned int num_peaks;
	unsigned int final_runs;
};

struct results *new_results(void)
{
	struct results *r = talloc(NULL, struct results);
	r->num_results = 0;
	r->overhead = (u64)-1;
	r->minimum = (u64)-1;
	r->maximum = 0;
	r->results = NULL;
	r->peaks = NULL;
	r->num_peaks = 0;
	r->final_runs = 0;
	return r;
}

void add_result(struct results *r, u64 res)
{
	r->results = talloc_realloc(r, r->results, u64, r->num_results+1);
	r->results[r->num_results] = res;
	r->num_results++;
	if (res < r->overhead)
		r->overhead = res;
	if (res < r->minimum)
		r->minimum = res;
	if (res > r->maximum)
		r->maximum = res;
}

/* We assume no overflow. */
static u64 average(unsigned int num, const u64 *results)
{
	unsigned int i;
	u64 total = 0;

	for (i = 0; i < num; i++)
		total += results[i];
	return total / num;
}

static u64 minimum_between(unsigned int num, const u64 *results,
			   u64 lower, u64 upper)
{
	unsigned int i;
	u64 min = (u64)-1;

	for (i = 0; i < num; i++) {
		if (results[i] < lower || results[i] > upper)
			continue;
		if (results[i] < min)
			min = results[i];
	}
	return min;
}

static u64 maximum_between(unsigned int num, const u64 *results,
			   u64 lower, u64 upper)
{
	unsigned int i;
	u64 max = 0;

	for (i = 0; i < num; i++) {
		if (results[i] < lower || results[i] > upper)
			continue;
		if (results[i] > max)
			max = results[i];
	}
	return max;
}

struct peak
{
	u64 start, end;
	unsigned int num_results;
};

static unsigned count_results(u64 start, u64 end, unsigned num, u64 *results)
{
	unsigned int total = 0, i;

	for (i = 0; i < num; i++)
		if (results[i] >= start && results[i] < end)
			total++;
	return total;
}

static struct peak *get_peaks(const struct results *r, unsigned int *num)
{
	u64 max, min;
	unsigned int i;
	struct peak *peaks, *p;

	max = r->maximum+1;
	min = r->minimum;

	/* Each peak represents 1% of min (round up). */
	*num = ((max - min) * 100 + min - 1) / min;
	p = peaks = talloc_array(r, struct peak, *num);

	for (i = 0; i < *num; i++) {
		p->start = min + (max - min) * i / *num;
		p->end = min + (max - min) * (i+1) / *num;
		/* FIXME: This is O(n^2) */
		p->num_results = count_results(p->start, p->end,
					       r->num_results, r->results);
		if (p->num_results)
			p++;
	}
	/* Adjust number to actual amount used. */
	*num = p - peaks;

	/* Look for consecutive peaks: we might be able to move boundaries. */
	for (i = 0; i + 1 < *num; i++) {
		if (peaks[i+1].start != peaks[i].end)
			continue;

		if (i + 2 == *num || peaks[i+1].end != peaks[i+2].start) {
			u64 lmin, lmax;
			lmin = minimum_between(r->num_results, r->results,
					       peaks[i].start,
					       peaks[i+1].end);
			lmax = maximum_between(r->num_results, r->results,
					       peaks[i].start,
					       peaks[i+1].end);
			if ((lmax - lmin) * 100 <= min) {
				peaks[i].start = lmin;
				peaks[i].end = lmax+1;
				delete_arr(peaks, *num, i+1, 1);
				(*num)--;
				i--;
			}
		}
	}

	/* Eliminate any peak <= 1%. */
	for (i = 0; i < *num; i++) {
		if (peaks[i].num_results * 100 <= r->num_results) {
			delete_arr(peaks, *num, i, 1);
			(*num)--;
			i--;
		}
	}

	/* Narrow the peaks. */
	for (i = 0; i < *num; i++) {
		peaks[i].start = minimum_between(r->num_results, r->results,
						 peaks[i].start,
						 peaks[i].end);
		peaks[i].end = maximum_between(r->num_results, r->results,
					       peaks[i].start,
					       peaks[i].end) + 1;
	}

	return peaks;
}

bool results_done(struct results *r, unsigned int *runs, bool rough)
{
	u64 avg;
	struct peak *peaks;
	unsigned i, num_peaks, num_sufficient;

	if (r->num_peaks)
		return true;

	if (r->num_results < MINIMUM_RUNS)
		return false;

	if (*runs == 0) {
		*runs = 1;
		goto reset_results;
	}

	/* We're aiming for at least 100x overhead. */
	avg = average(r->num_results, r->results);
	if (avg < r->overhead * 100) {
		u64 per_run = (avg - r->overhead) / *runs;

		/* Jump to approx how many runs we'd need... */
		do {
			*runs *= 2;
		} while (*runs * per_run < 100 * r->overhead);
	reset_results:
		r->num_results = 0;
		r->results = NULL;
		r->minimum = (u64)-1;
		r->maximum = 0;
		return false;
	}

	/* OK, the overhead is in the noise.  How are our results looking? */
	peaks = get_peaks(r, &num_peaks);

	/* How many peaks have at least MINIMUM_RUNS in them? */
	num_sufficient = 0;
	for (i = 0; i < num_peaks; i++)
		num_sufficient += (peaks[i].num_results >= MINIMUM_RUNS);

	/* Need all of them if !rough */
	if ((rough && num_sufficient == 0)
	    || (!rough && num_sufficient < num_peaks)) {
		    talloc_free(peaks);
		    return false;
	}

	r->peaks = peaks;
	r->num_peaks = num_peaks;
	r->final_runs = *runs;
	return true;
}

char *results_to_dist_summary(struct results *r)
{
	char *str = talloc_strdup(r, "");
	unsigned int i;
	struct peak *p = r->peaks;

	assert(p);
	for (i = 0; i < r->num_peaks; i++) {
		str = talloc_asprintf_append(str, "%s%llu:%u%%",
					     i > 0 ? ", " : "",
					     ((p[i].start+p[i].end)/2
					      - r->overhead)
					     / r->final_runs,
					     (p[i].num_results * 100
					      + r->num_results/2)
					     / r->num_results);
	}
	return str;
}

static int cmp_u64(const void *p1, const void *p2)
{
	/* We assume that the difference fits in an int. */
	return *(u64 *)p1 - *(u64 *)p2;
}

static u64 median(const u64 *vals, unsigned int num)
{
	u64 sorted[num];

	memcpy(sorted, vals, sizeof(sorted));
	qsort(sorted, num, sizeof(sorted[0]), cmp_u64);
	return sorted[num/2];
}

char *results_to_quick_summary(struct results *r)
{
	u64 min, max, med;

	med = (median(r->results,r->num_results)-r->overhead) / r->final_runs;
	min = (r->minimum - r->overhead) / r->final_runs;
	max = (r->maximum - r->overhead) / r->final_runs;
	return talloc_asprintf(r, "%llu (%llu - %llu)", med, min, max);
}

char *results_to_csv(struct results *r)
{
	char *str = talloc_strdup(r, "");
	unsigned int i;

	assert(r->num_peaks);
	for (i = 0; i < r->num_results; i++) {
		str = talloc_asprintf_append(str, "%s%llu",
					     i > 0 ? "," : "",
					     (r->results[i] - r->overhead)
					     / r->final_runs);
	}
	return str;
}
	
