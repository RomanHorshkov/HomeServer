/**
 * @file IT_http_man_time.c
 * @brief Timing integration tests for HTTP parser throughput measurements.
 */

#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif

#include <inttypes.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cmocka.h>

#include "http_man_test_utils.h"
#include "http_manager.h"

#define TIMING_RUNS 1000
#define WARMUP_RUNS 50

/* Buffer sizes */
const size_t sizes[] = {128, KiB(1U), KiB(4U), KiB(32U)};

static uint64_t timespec_diff_ns(const struct timespec* start, const struct timespec* end)
{
    uint64_t sec_diff = (uint64_t)(end->tv_sec - start->tv_sec);

    if(end->tv_nsec < start->tv_nsec)
    {
        /* Borrow from seconds */
        sec_diff -= 1;
        return (sec_diff * 1000000000ULL) + (1000000000ULL + end->tv_nsec - start->tv_nsec);
    }

    return (sec_diff * 1000000000ULL) + (uint64_t)(end->tv_nsec - start->tv_nsec);
}

static int compare_u64(const void* a, const void* b)
{
    const uint64_t lhs = *(const uint64_t*)a;
    const uint64_t rhs = *(const uint64_t*)b;
    if(lhs < rhs) return -1;
    if(lhs > rhs) return 1;
    return 0;
}

static void print_timing_stats(size_t size, const uint64_t* samples, size_t count)
{
    uint64_t sum     = 0;
    uint64_t minimum = UINT64_MAX;
    uint64_t maximum = 0;

    for(size_t idx = 0; idx < count; ++idx)
    {
        sum     += samples[idx];
        minimum  = samples[idx] < minimum ? samples[idx] : minimum;
        maximum  = samples[idx] > maximum ? samples[idx] : maximum;
    }

    uint64_t average = sum / count;
    uint64_t sorted[TIMING_RUNS];
    memcpy(sorted, samples, count * sizeof(sorted[0]));
    qsort(sorted, count, sizeof(sorted[0]), compare_u64);
    uint64_t median;
    if((count % 2) == 0)
    {
        median = (sorted[count / 2 - 1] + sorted[count / 2]) / 2;
    }
    else
    {
        median = sorted[count / 2];
    }

    printf("[timing] parsed %zu bytes | runs=%zu | min=%" PRIu64 " ns | median=%" PRIu64 " ns | avg=%" PRIu64 " ns | max=%" PRIu64 " ns\n",
           size, count, minimum, median, average, maximum);
}

static void test_http_manager_timing(void** state)
{
    (void)state;

    for(size_t idx = 0; idx < sizeof(sizes) / sizeof(sizes[0]); ++idx)
    {
        size_t message_len = 0;
        char*  message     = hs_build_http_request_exact_len(sizes[idx], &message_len);
        assert_non_null(message);
        assert_int_equal(message_len, sizes[idx]);

        llhttp_parser_t parser;
        assert_int_equal(http_man_init(&parser), STATUS_SUCCESS);

        /* Warmup */
        for(size_t run = 0; run < WARMUP_RUNS; ++run)
        {
            assert_int_equal(http_man_execute(&parser, message, message_len), STATUS_SUCCESS);
            http_man_reset(&parser);
        }

        uint64_t samples[TIMING_RUNS];
        for(size_t run = 0; run < TIMING_RUNS; ++run)
        {
            struct timespec start, end;
            clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start);
            assert_int_equal(http_man_execute(&parser, message, message_len), STATUS_SUCCESS);
            clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end);

            samples[run] = timespec_diff_ns(&start, &end);
            http_man_reset(&parser);
        }

        print_timing_stats(message_len, samples, TIMING_RUNS);
        http_man_reset(&parser);
        free(message);
    }
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_http_manager_timing),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
