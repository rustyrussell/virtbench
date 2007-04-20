#include "../results.c"
#include <assert.h>
#include <stdlib.h>

static void simple_test(void)
{
	struct results *r;

	r = new_results();

	add_result(r, 2000);
	assert(r->num_results == 1);
	assert(r->minimum == 2000);
	assert(r->results[0] == 2000);

	/* Trivial tests */
	assert(average(r->num_results, r->results) == 2000);
	assert(minimum(r->num_results, r->results) == 2000);
	assert(maximum(r->num_results, r->results) == 2000);
	assert(minimum_between(r->num_results, r->results, 0, 3000) == 2000);
	assert(maximum_between(r->num_results, r->results, 0, 3000) == 2000);
	assert(count_results(0, 3000, r->num_results, r->results) == 1);

	/* Add a second result. */
	add_result(r, 1000);
	assert(r->num_results == 2);
	assert(r->minimum == 1000);
	assert(r->results[0] == 2000);
	assert(r->results[1] == 1000);

	/* Trivial tests */
	assert(average(r->num_results, r->results) == 1500);
	assert(minimum(r->num_results, r->results) == 1000);
	assert(maximum(r->num_results, r->results) == 2000);
	assert(minimum_between(r->num_results, r->results, 0, 3000) == 1000);
	assert(minimum_between(r->num_results, r->results, 0, 1500) == 1000);
	assert(minimum_between(r->num_results, r->results, 1500, 3000)
	       == 2000);
	assert(maximum_between(r->num_results, r->results, 0, 3000) == 2000);
	assert(maximum_between(r->num_results, r->results, 0, 1500) == 1000);
	assert(maximum_between(r->num_results, r->results, 1500, 3000)
	       == 2000);
	assert(count_results(0, 3000, r->num_results, r->results) == 2);
	assert(count_results(0, 1500, r->num_results, r->results) == 1);
	assert(count_results(1500, 3000, r->num_results, r->results) == 1);

	talloc_free(r);
}

static struct results *onepeak_test(u64 (*result)(unsigned int runs,
						  unsigned int num),
				    unsigned int expected_runs,
				    u64 min, u64 max,
				    unsigned int expected_results)
{
	unsigned int i, num, runs = 0;
	struct results *r = new_results();
	struct peak *peak;

	for (i = 0; i < expected_runs; i++) {
		/* We expect MINIMUM_RUNS at 0, same at 1, then at 1024. */
		if (i <= MINIMUM_RUNS)
			assert(runs == 0);
		else if (i <= MINIMUM_RUNS*2)
			assert(runs == 1);
		else
			assert(runs == 16);
		if (results_done(r, &runs, false))
			assert(0);
		add_result(r, result(runs, i));
	}

	/* Examine peaks: we should have only one. */
	peak = get_peaks(r, &num);
	assert(num == 1);
	assert(peak->start <= min);
	assert(peak->end > max);
	assert(peak->num_results == expected_results);
	talloc_free(peak);

	/* This should now give us an answer. */
	assert(results_done(r, &runs, false));
	return r;
}

static u64 uniform_result(unsigned int runs, unsigned int num)
{
	/* Overhead is 100, results are 1000 per run */
	return 100 + runs * 1000;
}

static u64 low_noise_result(unsigned int runs, unsigned int num)
{
	/* Overhead is 100, results are 1000 per run (within 1%) */
	return 100 + runs * (1000 + num%3 - 1);
}

/* We return a complete outlier the first time for the serious run. */
static u64 high_noise_result(unsigned int runs, unsigned int num)
{
	if (num == MINIMUM_RUNS*2) {
		assert(runs == 16);
		return 100 + 2 * runs * 1000;
	}
	return low_noise_result(runs, num);
}

static void consistent_tests(void)
{
	struct results *r;

	r = onepeak_test(&uniform_result, MINIMUM_RUNS*3,
			 16100, 16100, MINIMUM_RUNS);
	assert(streq(results_to_dist_summary(r), "1000:100%"));
	assert(streq(results_to_quick_summary(r), "1000 (1000 - 1000)"));
	assert(streq(results_to_csv(r),
		     "1000,1000,1000,1000,1000,1000,1000,1000,1000,1000"));
	talloc_free(r);

	r = onepeak_test(&low_noise_result, MINIMUM_RUNS*3,
			 100 + 16*999, 100 + 16*1001, MINIMUM_RUNS);
	assert(streq(results_to_dist_summary(r), "1000:100%"));
	assert(streq(results_to_quick_summary(r), "1000 (999 - 1001)"));
	assert(streq(results_to_csv(r),
		     "1001,999,1000,1001,999,1000,1001,999,1000,1001"));
	talloc_free(r);

	/* This will need to run enough to make sure 1 bogus run == 1% */
	r = onepeak_test(&high_noise_result, MINIMUM_RUNS*2 + 100,
			 100 + 16*999, 100 + 16*1001, 99);
	assert(streq(results_to_dist_summary(r), "1000:99%"));
	assert(streq(results_to_quick_summary(r), "1000 (999 - 2000)"));
	talloc_free(r);
}

/* Return, with equal probability, a whole range of values */
static void flat_test(void)
{
	unsigned int i = 0, num, runs = 0;
	struct results *r = new_results();
	struct peak *peak;

	do {
		add_result(r, 100 + runs*(100 + (i%10)));
		if (runs > 1)
			i++;
	} while (!results_done(r, &runs, false));

	peak = get_peaks(r, &num);
	assert(num == 9);
	for (i = 0; i < num; i++) {
		assert(peak[i].num_results >= MINIMUM_RUNS);
		assert((peak[i].end - peak[i].start) * 100 <= 100+runs*100);
	}
	talloc_free(peak);
	talloc_free(r);
}

/* Twin peaks */
static void dual_test(void)
{
	unsigned int i = 0, num, runs = 0;
	struct results *r = new_results();
	struct peak *peak;

	do {
		add_result(r, 100 + runs*(100 + (i%2)*5));
		if (runs > 1)
			i++;
	} while (!results_done(r, &runs, false));

	peak = get_peaks(r, &num);
	assert(num == 2);
	talloc_free(peak);

	assert(streq(results_to_dist_summary(r), "100:50%, 105:50%"));
	talloc_free(r);
}

int main()
{
	unsigned int size;

	talloc_enable_null_tracking();
	size = talloc_total_size(NULL);
	simple_test();
	assert(talloc_total_size(NULL) == size);
	consistent_tests();
	assert(talloc_total_size(NULL) == size);
	flat_test();
	assert(talloc_total_size(NULL) == size);
	dual_test();
	assert(talloc_total_size(NULL) == size);
	return 0;
}
