/**
 * @file operator_lifecycle_tests.c
 *
 * @brief Unit tests for the operator init/startup state machine (operator.h's OPERATOR_STATUS_INITIALIZING
 *        / _INIT_FAILED, operator.c's operator_init()/operator_thread(), worker.c's worker_run() wait loop)
 *        — the split introduced to move client-parser allocation off the boot thread onto each operator's
 *        own pinned thread (MEMORY_MODEL.md §4.3), without breaking worker_init()'s fail-fast contract.
 *
 * The interesting property under test isn't "does operator_init() return 0" — it's the CAS-guarded
 * INITIALIZING -> {ACTIVE, INIT_FAILED} transition racing against a concurrent operator_request_shutdown():
 * a plain (non-CAS) store on either side would either silently resurrect a requested shutdown (operator
 * serves traffic it was told to stop before starting) or, worse, get clobbered back to ACTIVE right after
 * a shutdown request, which makes worker_destroy()'s pthread_join() wait forever for a shutdown signal
 * that already came and went. test_operator_thread_honors_concurrent_shutdown_never_hangs is a repeated
 * stress test of exactly that race, bounded with pthread_timedjoin_np so a regression FAILS instead of
 * hanging the whole test binary.
 *
 * @author  Roman Horshkov <github.com/RomanHorshkov>
 * @date    jul 2026
 * (c) 2026
 */

#define _GNU_SOURCE /* pthread_timedjoin_np — must precede every system include */

/*****************************************************************************************************************************************
 * INCLUDES
 *****************************************************************************************************************************************
 */
/* cmocka needs these four BEFORE it — keep this order. */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>

#include <cmocka.h>

#include <pthread.h>
#include <string.h>
#include <time.h>

#include <db_server/core/worker/operator/operator.h>
#include <db_server/core/worker/worker.h>

/*****************************************************************************************************************************************
 * PRIVATE DEFINES
 *****************************************************************************************************************************************
 */

#define TEST_RING_CAPACITY  16u
#define TEST_JOIN_TIMEOUT_S 2

/* How many times to race operator_thread() against a concurrent shutdown request. Each iteration is a
 * handful of client-parser mallocs plus a few atomic ops — cheap enough to run a real stress count. */
#define RACE_ITERATIONS     200

/*****************************************************************************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 *****************************************************************************************************************************************
 */

/** @brief pthread_join() bounded by TEST_JOIN_TIMEOUT_S — a regression that reintroduces the shutdown
 *         race would hang forever here instead of failing the assertion; this turns that hang into a
 *         normal test failure. */
static int _bounded_join(pthread_t thread);

static void test_operator_init_leaves_status_initializing(void** state);
static void test_operator_thread_happy_path_reaches_active_with_parsers(void** state);
static void test_operator_thread_honors_concurrent_shutdown_never_hangs(void** state);
static void test_worker_init_run_destroy_happy_path(void** state);

/*****************************************************************************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 *****************************************************************************************************************************************
 */

static int _bounded_join(pthread_t thread)
{
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += TEST_JOIN_TIMEOUT_S;
    return pthread_timedjoin_np(thread, NULL, &deadline);
}

/** @brief operator_init() alone (the boot-thread half) must NOT allocate any client parser — that
 *         allocation moved to operator_thread() specifically to fix the NUMA first-touch gap. */
static void test_operator_init_leaves_status_initializing(void** state)
{
    (void)state;
    operator_t op;
    memset(&op, 0, sizeof op);

    assert_int_equal(operator_init(&op, 0u, TEST_RING_CAPACITY), STATUS_SUCCESS);
    assert_int_equal(op.status, OPERATOR_STATUS_INITIALIZING);
    assert_non_null(op.ring);
    assert_int_not_equal(op.wakeup_ctx.fd, -1);
    assert_int_not_equal(op.timer_fd, -1);
    assert_null(op.clients[0].http_parser);
    assert_null(op.clients[WORKER_MAX_CLIENTS - 1u].http_parser);

    operator_shutdown(&op);
}

/** @brief The normal path: operator_thread() pins, first-touches the response arena, allocates every
 *         client's parser, and transitions INITIALIZING -> ACTIVE — all on its own thread, none of it
 *         on the caller's (this test's) thread. */
static void test_operator_thread_happy_path_reaches_active_with_parsers(void** state)
{
    (void)state;
    operator_t op;
    memset(&op, 0, sizeof op);
    assert_int_equal(operator_init(&op, 1u, TEST_RING_CAPACITY), STATUS_SUCCESS);

    pthread_t thread;
    assert_int_equal(pthread_create(&thread, NULL, operator_thread, &op), 0);

    /* Bounded poll for the thread to leave INITIALIZING — parser allocation is a handful of mallocs,
     * this should resolve in well under a millisecond in practice. */
    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += TEST_JOIN_TIMEOUT_S;
    operator_status_t st;
    while((st = op.status) == OPERATOR_STATUS_INITIALIZING)
    {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        assert_true(now.tv_sec < deadline.tv_sec);
        struct timespec poll_interval = {.tv_sec = 0, .tv_nsec = 100000L};
        nanosleep(&poll_interval, NULL);
    }

    assert_int_equal(st, OPERATOR_STATUS_ACTIVE);
    assert_non_null(op.clients[0].http_parser);
    assert_non_null(op.clients[WORKER_MAX_CLIENTS - 1u].http_parser);

    /* Exact worker_destroy() sequence: request shutdown, join, THEN destroy resources. */
    operator_request_shutdown(&op);
    assert_int_equal(_bounded_join(thread), 0);
    operator_shutdown(&op);
}

/** @brief The race: request shutdown the instant the thread is created, before it can possibly have
 *         finished (or even started) its client-parser loop. Repeated many times to sample both sides
 *         of the CAS race (thread wins / request_shutdown wins). Every iteration must join within the
 *         bound — a hang here means the CAS guard regressed and worker_destroy() would deadlock in
 *         production the same way. */
static void test_operator_thread_honors_concurrent_shutdown_never_hangs(void** state)
{
    (void)state;
    for(int i = 0; i < RACE_ITERATIONS; i++)
    {
        operator_t op;
        memset(&op, 0, sizeof op);
        assert_int_equal(operator_init(&op, (uint8_t)(i % 256), TEST_RING_CAPACITY), STATUS_SUCCESS);

        pthread_t thread;
        assert_int_equal(pthread_create(&thread, NULL, operator_thread, &op), 0);
        operator_request_shutdown(&op); /* racing against the thread's own init, on purpose */

        assert_int_equal(_bounded_join(thread), 0);

        /* Whichever side of the race won, the outcome converges to SHUTDOWN: either
         * request_shutdown()'s store landed after a successful CAS (overwriting ACTIVE), or it landed
         * first and the CAS itself failed, in which case the thread never touches status again. */
        assert_int_equal(op.status, OPERATOR_STATUS_SHUTDOWN);

        operator_shutdown(&op); /* safe regardless of how far client-parser init got (op1) */
    }
}

/** @brief The full boot sequence exercised end-to-end: worker_init() (boot-thread half) ->
 *         worker_run() (spawns + WAITS for every operator's post-pin init) -> worker_destroy(). Forces
 *         a single operator (cpu_count<=2) to keep the test fast and deterministic. */
static void test_worker_init_run_destroy_happy_path(void** state)
{
    (void)state;
    assert_int_equal(worker_init(2u), STATUS_SUCCESS);
    assert_int_equal(worker_get_operators_count(), 1u);

    /* worker_run() only returns SUCCESS after every operator reports ACTIVE — no external poll needed
     * here, that IS the property under test. */
    assert_int_equal(worker_run(), STATUS_SUCCESS);

    worker_destroy();
    assert_int_equal(worker_get_operators_count(), 0u);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_operator_init_leaves_status_initializing),
        cmocka_unit_test(test_operator_thread_happy_path_reaches_active_with_parsers),
        cmocka_unit_test(test_operator_thread_honors_concurrent_shutdown_never_hangs),
        cmocka_unit_test(test_worker_init_run_destroy_happy_path),
    };
    return cmocka_run_group_tests_name("operator_lifecycle", tests, NULL, NULL);
}
