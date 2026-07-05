/**
 * @file http_man_test_utils.h
 * @brief HTTP parser integration-test utility declarations.
 */

#ifndef HTTP_MAN_TEST_UTILS_H
#define HTTP_MAN_TEST_UTILS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Build an HTTP/1.1 request whose total length matches @p target_len.
 *
 * The function fills the request with a minimal set of headers followed by synthetic `X-FILL-XX` headers until the buffer reaches exactly
 * the requested size. Returns NULL if the requested size cannot be reached.
 *
 * The caller is responsible for freeing the returned buffer.
 *
 * @param target_len Requested total size of the HTTP message.
 * @param out_len Optional output of the actual generated length (will be equal to @p target_len on success).
 * @return Pointer to the allocated message, or NULL on failure.
 */
char* hs_build_http_request_exact_len(size_t target_len, size_t* out_len);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_MAN_TEST_UTILS_H */
