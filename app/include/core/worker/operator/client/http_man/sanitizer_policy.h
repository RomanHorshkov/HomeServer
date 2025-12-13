/**
 * @file sanitizer_policy.h
 *
 * @brief HTTP request sanitizer policy definitions.
 *
 * Each policy is a 256-bit bitmap: 1 bit per possible byte (0..255).
 * If a bit is 1 => character is allowed for that component.
 * If a bit is 0 => character is forbidden.
 *
 * We then have 3 policies:
 *   - char_policy_http_path         : allowed chars in URI path (and query)
 *   - char_policy_http_header_name  : allowed chars in header names
 *   - char_policy_http_header_value : allowed chars in header values
 *
 * The actual validation is done via SANITIZER_POLICY_*_VALID() macros.
 *
 * (c) 2025 Roman Horshkov
 */
#ifndef SERVER_HTTP_SANITIZER_POLICY_H
#define SERVER_HTTP_SANITIZER_POLICY_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/****************************************************************************
 * INCLUDES
 ****************************************************************************
 */

#include <stdint.h> /* uint8_t */
#include <stddef.h> /* size_t */

/****************************************************************************
 * DEFINES
 ****************************************************************************
 */

/*
 * Generic policy table: 1 bit per possible byte (0..255)
 * 256 bits => 32 bytes
 */

#define POLICY_BITS_PER_TABLE   256u
#define POLICY_BYTES_PER_TABLE  (POLICY_BITS_PER_TABLE / 8u)

/*
 * Bit layout:
 *
 *   For a given unsigned char c:
 *
 *     byte index  = c >> 3        (c / 8)
 *     bit index   = c & 0x07      (c % 8)
 *     bit mask    = 1 << bit_index
 *
 *   If (table[byte_index] & mask) != 0 => character is allowed.
 */

/**
 * @brief Get the index into the policy table for a character.
 */
#define POLICY_IDX(ch)   ((uint8_t)(ch) >> 3)

/**
 * @brief Get bit shift for a character in the policy table.
 */
#define POLICY_SHIFT(ch) ((uint8_t)(ch) & 0x07u)

/**
 * @brief Get the bitmask for a character in the policy table.
 */
#define POLICY_MASK(ch)  ((uint8_t)(1u << POLICY_SHIFT(ch)))

/**
 * @brief Policy table type: 256 bits = 32 bytes
 */
typedef uint8_t policy_table_t[POLICY_BYTES_PER_TABLE];

/****************************************************************************
 * POLICY TABLES
 ****************************************************************************
 */

/*
 * char_policy_http_path:
 *
 * Allowed:
 *   - '0'..'9'
 *   - 'A'..'Z'
 *   - 'a'..'z'
 *   - '/', '-', '_', '.', '~', '?', '=', '&', '+', '%'
 *
 * Everything else, including control chars, is forbidden.
 */
static const policy_table_t char_policy_http_path = {
    /* 0x00–0x1F */ 0x00, 0x00, 0x00, 0x00,
    /* 0x20–0x3F */ 0x60, 0xE8, 0xFF, 0xA3,
    /* 0x40–0x5F */ 0xFE, 0xFF, 0xFF, 0x87,
    /* 0x60–0x7F */ 0xFE, 0xFF, 0xFF, 0x47,
    /* 0x80–0x9F */ 0x00, 0x00, 0x00, 0x00,
    /* 0xA0–0xBF */ 0x00, 0x00, 0x00, 0x00,
    /* 0xC0–0xDF */ 0x00, 0x00, 0x00, 0x00,
    /* 0xE0–0xFF */ 0x00, 0x00, 0x00, 0x00
};

/*
 * char_policy_http_header_name:
 *
 * Allowed:
 *   - '0'..'9'
 *   - 'A'..'Z'
 *   - 'a'..'z'
 *   - '-' and '_'
 *
 * No spaces, no other punctuation, no control chars.
 */
static const policy_table_t char_policy_http_header_name = {
    /* 0x00–0x1F */ 0x00, 0x00, 0x00, 0x00,
    /* 0x20–0x3F */ 0x00, 0x20, 0xFF, 0x03,
    /* 0x40–0x5F */ 0xFE, 0xFF, 0xFF, 0x87,
    /* 0x60–0x7F */ 0xFE, 0xFF, 0xFF, 0x07,
    /* 0x80–0x9F */ 0x00, 0x00, 0x00, 0x00,
    /* 0xA0–0xBF */ 0x00, 0x00, 0x00, 0x00,
    /* 0xC0–0xDF */ 0x00, 0x00, 0x00, 0x00,
    /* 0xE0–0xFF */ 0x00, 0x00, 0x00, 0x00
};

/*
 * char_policy_http_header_value:
 *
 * Allowed:
 *   - HTAB (0x09)   // optional, but we allow it
 *   - ' ' (0x20) .. '~' (0x7E)
 *
 * i.e. all printable ASCII except control chars and DEL (0x7F),
 * **plus** HTAB.
 */
static const policy_table_t char_policy_http_header_value = {
    /* 0x00–0x1F */ 0x00, 0x02, 0x00, 0x00,
    /* 0x20–0x3F */ 0xFF, 0xFF, 0xFF, 0xFF,
    /* 0x40–0x5F */ 0xFF, 0xFF, 0xFF, 0xFF,
    /* 0x60–0x7F */ 0xFF, 0xFF, 0xFF, 0x7F,
    /* 0x80–0x9F */ 0x00, 0x00, 0x00, 0x00,
    /* 0xA0–0xBF */ 0x00, 0x00, 0x00, 0x00,
    /* 0xC0–0xDF */ 0x00, 0x00, 0x00, 0x00,
    /* 0xE0–0xFF */ 0x00, 0x00, 0x00, 0x00
};

/****************************************************************************
 * MACROS
 ****************************************************************************
 */

/* Low-level check: a single character is allowed by a table */
#define POLICY_CHAR_OK(table, ch) \
    (((table)[POLICY_IDX((unsigned char)(ch))] & POLICY_MASK((unsigned char)(ch))) != 0u)

static inline int sanitizer_policy_check(const policy_table_t tbl, const void *buf, size_t len)
{
    const unsigned char *san_p = (const unsigned char *)buf;
    for(size_t san_i = 0; san_i < len; ++san_i)
    {
        if(!POLICY_CHAR_OK(tbl, san_p[san_i]))
        {
            return 0;
        }
    }
    return 1;
}

#define SANITIZER_POLICY_CHECK(policy_tbl, buf, len) \
    sanitizer_policy_check((policy_tbl), (buf), (len))

/* User-facing macros, specific policies. */

#define SANITIZER_POLICY_PATH_VALID(ptr, len) \
    SANITIZER_POLICY_CHECK(char_policy_http_path, (ptr), (len))

#define SANITIZER_POLICY_HDR_NAME_VALID(ptr, len) \
    SANITIZER_POLICY_CHECK(char_policy_http_header_name, (ptr), (len))

#define SANITIZER_POLICY_HDR_VALUE_VALID(ptr, len) \
    SANITIZER_POLICY_CHECK(char_policy_http_header_value, (ptr), (len))

#endif /* SERVER_HTTP_SANITIZER_POLICY_H */
