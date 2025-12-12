#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>

#include <cmocka.h>

#include "http_manager.h"
#include "http_man_test_utils.h"

static int parser_setup(void **state)
{
    llhttp_parser_t *parser = calloc(1, sizeof(*parser));
    if (!parser)
    {
        return -1;
    }

    if (http_man_init(parser) != STATUS_SUCCESS)
    {
        free(parser);
        return -1;
    }

    *state = parser;
    return 0;
}

static int parser_teardown(void **state)
{
    llhttp_parser_t *parser = *state;
    if (parser)
    {
        http_man_reset(parser);
        free(parser);
        *state = NULL;
    }
    return 0;
}

static int execute_in_chunks(llhttp_parser_t *parser, const char *buffer, size_t length)
{
    static const size_t weights[] = {1, 2, 4, 8, 16};
    const size_t total_weight = 31;
    size_t offset = 0;

    for (size_t idx = 0; idx < 5 && offset < length; ++idx)
    {
        size_t chunk;
        if (idx < 4)
        {
            chunk = (length * weights[idx]) / total_weight;
            if (chunk == 0)
            {
                chunk = 1;
            }
            if (chunk > length - offset)
            {
                chunk = length - offset;
            }
        }
        else
        {
            chunk = length - offset;
        }

        if (chunk == 0)
        {
            break;
        }

        int rc = http_man_execute(parser, buffer + offset, chunk);
        offset += chunk;
        if (rc != STATUS_SUCCESS)
        {
            return rc;
        }
    }

    return offset == length ? STATUS_SUCCESS : STATUS_FAILURE;
}

static void test_single_read_small(void **state)
{
    llhttp_parser_t *parser = *state;
    size_t message_len = 0;
    char *message = hs_build_http_request_exact_len(100, &message_len);
    assert_non_null(message);

    assert_int_equal(message_len, 100);
    assert_int_equal(http_man_execute(parser, message, message_len), STATUS_SUCCESS);
    assert_int_equal(parser->parser_ctx.req.message_complete, 1);

    free(message);
}

static void test_single_read_limit(void **state)
{
    llhttp_parser_t *parser = *state;
    size_t message_len = 0;
    size_t target = HTTP_RECEIVE_BUFFER_LEN;
    char *message = hs_build_http_request_exact_len(target, &message_len);
    assert_non_null(message);

    assert_int_equal(message_len, target);
    assert_int_equal(http_man_execute(parser, message, message_len), STATUS_SUCCESS);
    assert_int_equal(parser->parser_ctx.req.message_complete, 1);

    free(message);
}

static void test_single_read_overflow(void **state)
{
    llhttp_parser_t *parser = *state;
    size_t message_len = 0;
    size_t target = HTTP_RECEIVE_BUFFER_LEN + 1;
    char *message = hs_build_http_request_exact_len(target, &message_len);
    assert_non_null(message);

    assert_int_equal(message_len, target);
    assert_int_equal(http_man_execute(parser, message, message_len), STATUS_FAILURE);
    assert_int_equal(parser->parser_ctx.req.message_complete, 0);

    free(message);
}

static void test_multi_read_small(void **state)
{
    llhttp_parser_t *parser = *state;
    size_t message_len = 0;
    char *message = hs_build_http_request_exact_len(100, &message_len);
    assert_non_null(message);

    assert_int_equal(execute_in_chunks(parser, message, message_len), STATUS_SUCCESS);
    assert_int_equal(parser->parser_ctx.req.message_complete, 1);

    free(message);
}

static void test_multi_read_limit(void **state)
{
    llhttp_parser_t *parser = *state;
    size_t message_len = 0;
    size_t target = HTTP_RECEIVE_BUFFER_LEN;
    char *message = hs_build_http_request_exact_len(target, &message_len);
    assert_non_null(message);

    assert_int_equal(execute_in_chunks(parser, message, message_len), STATUS_SUCCESS);
    assert_int_equal(parser->parser_ctx.req.message_complete, 1);

    free(message);
}

static void test_multi_read_overflow(void **state)
{
    llhttp_parser_t *parser = *state;
    size_t message_len = 0;
    size_t target = HTTP_RECEIVE_BUFFER_LEN + 1;
    char *message = hs_build_http_request_exact_len(target, &message_len);
    assert_non_null(message);

    assert_int_equal(execute_in_chunks(parser, message, message_len), STATUS_FAILURE);
    assert_int_equal(parser->parser_ctx.req.message_complete, 0);

    free(message);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_single_read_small, parser_setup, parser_teardown),
        cmocka_unit_test_setup_teardown(test_single_read_limit, parser_setup, parser_teardown),
        cmocka_unit_test_setup_teardown(test_single_read_overflow, parser_setup, parser_teardown),
        cmocka_unit_test_setup_teardown(test_multi_read_small, parser_setup, parser_teardown),
        cmocka_unit_test_setup_teardown(test_multi_read_limit, parser_setup, parser_teardown),
        cmocka_unit_test_setup_teardown(test_multi_read_overflow, parser_setup, parser_teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
