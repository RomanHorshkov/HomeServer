/**
 * @file folder_zip_pump.c
 * @brief Streams a folder as a ZIP straight to the client — the DB_server half of "Download folder as ZIP".
 *
 * nginx has no mod_zip, so the archive is built LIVE here (on the isolated upload pool, so a long download never
 * blocks an API operator): DB_app validates the signed link and enumerates the files (db_app_folder_zip_*), and
 * this pump writes the ZIP framing while reading each blob once. STORE (no compression — media is already
 * compressed), 32-bit ZIP with data descriptors; every size is known from metadata up front, so Content-Length
 * is exact and the CRC (the only unknown) rides the data descriptor. Bounded memory: one cli->buf-sized slice at
 * a time. v1 rejects a file/total ≥ 4 GiB in DB_app (ZIP64 is the follow-up).
 *
 * @author  Roman Horshkov <github.com/RomanHorshkov>
 */

/*****************************************************************************************************************************************
 * PRIVATE INCLUDES
 *****************************************************************************************************************************************
 */
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <DB_http/DB_http.h>
#include <emlog.h>

#include <db_app/files/folder_zip.h>

#include <db_server/core/worker/operator/client/folder_zip_pump.h>
#include <db_server/core/worker/operator/client/response_writer.h>

/*****************************************************************************************************************************************
 * PRIVATE DEFINES
 *****************************************************************************************************************************************
 */
#define LOG_TAG "srv_zip"

/** The public path prefix the gate/pump claims (query text never reaches the gate — nginx matches the location verbatim). */
#define ZIP_PATH_PREFIX "/api/app/files/folders/zip/"

/*****************************************************************************************************************************************
 * CRC-32 (IEEE, zlib-compatible incremental) + little-endian encoders
 *****************************************************************************************************************************************
 */
static uint32_t       g_crc_table[256];
static pthread_once_t g_crc_once = PTHREAD_ONCE_INIT;

static void _crc_init(void)
{
    for(uint32_t n = 0u; n < 256u; n++)
    {
        uint32_t c = n;
        for(int k = 0; k < 8; k++)
        {
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        g_crc_table[n] = c;
    }
}

/** @brief Continue a zlib-style CRC (start at 0). Internally re-inverts, so repeated calls chain correctly. */
static uint32_t _crc_update(uint32_t crc, const uint8_t* p, size_t n)
{
    crc ^= 0xFFFFFFFFu;
    for(size_t i = 0u; i < n; i++)
    {
        crc = g_crc_table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

static void _le16(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}
static void _le32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}
static void _le64(uint8_t* p, uint64_t v)
{
    for(unsigned i = 0u; i < 8u; i++)
    {
        p[i] = (uint8_t)((v >> (8u * i)) & 0xFFu);
    }
}

/** @brief Write the whole buffer or fail. The socket is non-blocking, so poll for writability on EAGAIN
 *         (never busy-spin a core on a slow client). Returns 0 on success, -1 on a failed write / stall. */
static int _write_all(int fd, const void* buf, size_t len)
{
    const uint8_t* p    = buf;
    size_t         sent = 0u;
    while(sent < len)
    {
        ssize_t n = write(fd, p + sent, len - sent);
        if(n > 0)
        {
            sent += (size_t)n;
            continue;
        }
        if(n < 0 && errno == EINTR)
        {
            continue;
        }
        if(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            struct pollfd pfd = {.fd = fd, .events = POLLOUT};
            if(poll(&pfd, 1, 30000) <= 0)
            {
                return -1; /* 30 s to drain, else the peer is gone/stuck */
            }
            continue;
        }
        return -1;
    }
    return 0;
}

/*****************************************************************************************************************************************
 * PUBLIC
 *****************************************************************************************************************************************
 */

int folder_zip_is_zip_path(sv_t path)
{
    const size_t pl = sizeof ZIP_PATH_PREFIX - 1u;
    return (path.n > pl && memcmp(path.p, ZIP_PATH_PREFIX, pl) == 0) ? 1 : 0;
}

/* Fixed ZIP64 record sizes (always ZIP64, so any file/archive size works uniformly). */
#define ZIP_LH      30u /* local file header (fixed part)                              */
#define ZIP_LX      20u /* local ZIP64 extra field: 4 header + 16 (uncomp8, comp8)     */
#define ZIP_DD      24u /* ZIP64 data descriptor: sig4 + crc4 + comp8 + uncomp8        */
#define ZIP_CD      46u /* central directory header (fixed part)                       */
#define ZIP_CX      28u /* central ZIP64 extra field: 4 header + 24 (uncomp8,comp8,off8)*/
#define ZIP_Z64EOCD 56u /* ZIP64 end-of-central-directory record                       */
#define ZIP_Z64LOC  20u /* ZIP64 EOCD locator                                          */
#define ZIP_EOCD    22u /* end-of-central-directory record                             */

int client_folder_zip_pump(client_t* cli, DB_app_request_t* req)
{
    pthread_once(&g_crc_once, _crc_init);

    /* 1. Validate the link + enumerate (DB_app owns access + the filesystem). On failure it filled a response. */
    DB_app_response_t    res;
    db_app_response_init(&res);
    db_app_folder_zip_t* h = NULL;
    if(db_app_folder_zip_begin(req, &res, &h) != DB_APP_OK)
    {
        int rc = response_writer_send(cli, &res);
        db_app_response_clear(&res);
        return rc;
    }
    db_app_response_clear(&res);

    const size_t n = db_app_folder_zip_count(h);

    /* 2. Content-Length is exact (always-ZIP64, so any size works):
     *    Σ(local hdr + local zip64 extra + name + data + zip64 descriptor) + Σ(central hdr + zip64 extra + name)
     *    + ZIP64 EOCD + ZIP64 locator + EOCD. */
    uint64_t body     = 0u;
    uint64_t cd_bytes = 0u;
    for(size_t i = 0u; i < n; i++)
    {
        size_t   nl = 0u;
        uint64_t sz = 0u;
        (void)db_app_folder_zip_entry(h, i, NULL, &nl, &sz);
        body += (uint64_t)ZIP_LH + ZIP_LX + nl + sz + ZIP_DD;
        cd_bytes += (uint64_t)ZIP_CD + ZIP_CX + nl;
    }
    const uint64_t cd_offset  = body;
    const uint64_t z64_eocd   = body + cd_bytes; /* offset of the ZIP64 EOCD record */
    const uint64_t total      = body + cd_bytes + ZIP_Z64EOCD + ZIP_Z64LOC + ZIP_EOCD;

    /* 3. Response headers. filename is sanitized already; strip quote/CR/LF for the header value. */
    const char* arch = db_app_folder_zip_archive_name(h);
    char        fname[80];
    size_t      fw = 0u;
    for(size_t i = 0u; arch[i] != '\0' && fw + 1u < sizeof fname; i++)
    {
        char c = arch[i];
        if(c != '"' && c != '\r' && c != '\n')
        {
            fname[fw++] = c;
        }
    }
    fname[fw] = '\0';

    char   head[320];
    int    hn = snprintf(head, sizeof head,
                         "HTTP/1.1 200 OK\r\nContent-Type: application/zip\r\nContent-Length: %llu\r\n"
                         "Content-Disposition: attachment; filename=\"%s\"\r\nCache-Control: private, no-store\r\nConnection: close\r\n\r\n",
                         (unsigned long long)total, fname);
    if(hn <= 0 || (size_t)hn >= sizeof head || _write_all(cli->ctx.fd, head, (size_t)hn) != 0)
    {
        EML_WARN(LOG_TAG, "fd %d: zip header write failed", cli->ctx.fd);
        db_app_folder_zip_release(h);
        return STATUS_FAILURE;
    }

    /* 4. Stream each entry: local header + data (CRC as we go) + data descriptor. Record crc + offset for the CD. */
    uint64_t off = 0u;
    for(size_t i = 0u; i < n; i++)
    {
        const char* name = NULL;
        size_t      nl   = 0u;
        uint64_t    sz   = 0u;
        (void)db_app_folder_zip_entry(h, i, &name, &nl, &sz);

        int fd = -1;
        if(db_app_folder_zip_open(h, i, &fd) != DB_APP_OK)
        {
            EML_ERROR(LOG_TAG, "fd %d: zip blob open failed (entry %zu) — aborting stream", cli->ctx.fd, i);
            db_app_folder_zip_release(h);
            return STATUS_FAILURE; /* headers already sent → truncate the connection so the client sees an error */
        }

        uint8_t lh[ZIP_LH];
        _le32(lh + 0, 0x04034b50u);
        _le16(lh + 4, 45u);       /* version needed: 4.5 (ZIP64) */
        _le16(lh + 6, 0x0808u);   /* flags: bit3 data descriptor + bit11 UTF-8 name */
        _le16(lh + 8, 0u);        /* method: store */
        _le16(lh + 10, 0u);       /* mod time */
        _le16(lh + 12, 0x0021u);  /* mod date: 1980-01-01 (0 is invalid) */
        _le32(lh + 14, 0u);       /* crc (in descriptor) */
        _le32(lh + 18, 0xFFFFFFFFu); /* comp size → see ZIP64 extra / descriptor */
        _le32(lh + 22, 0xFFFFFFFFu); /* uncomp size → see ZIP64 extra / descriptor */
        _le16(lh + 26, (uint16_t)nl);
        _le16(lh + 28, ZIP_LX);   /* extra len (the ZIP64 field) */
        /* local ZIP64 extra: we KNOW the sizes up front, so write the REAL values (base fields are 0xFFFFFFFF,
         * so a strict reader takes them from here) — only the CRC is deferred to the data descriptor (bit 3). */
        uint8_t lx[ZIP_LX];
        _le16(lx + 0, 0x0001u);
        _le16(lx + 2, 16u);       /* data size: uncomp(8) + comp(8) */
        _le64(lx + 4, sz);        /* uncompressed */
        _le64(lx + 12, sz);       /* compressed (STORE: == uncompressed) */

        const uint64_t entry_off = off;
        if(_write_all(cli->ctx.fd, lh, ZIP_LH) != 0 || _write_all(cli->ctx.fd, name, nl) != 0 ||
           _write_all(cli->ctx.fd, lx, ZIP_LX) != 0)
        {
            (void)close(fd);
            db_app_folder_zip_release(h);
            return STATUS_FAILURE;
        }
        off += (uint64_t)ZIP_LH + nl + ZIP_LX;

        uint32_t crc       = 0u;
        uint64_t remaining = sz;
        int      failed    = 0;
        while(remaining > 0u)
        {
            size_t  want = remaining < sizeof cli->buf ? (size_t)remaining : sizeof cli->buf;
            ssize_t r    = read(fd, cli->buf, want);
            if(r > 0)
            {
                crc = _crc_update(crc, (const uint8_t*)cli->buf, (size_t)r);
                if(_write_all(cli->ctx.fd, cli->buf, (size_t)r) != 0)
                {
                    failed = 1;
                    break;
                }
                remaining -= (uint64_t)r;
                continue;
            }
            if(r < 0 && (errno == EINTR || errno == EAGAIN))
            {
                continue;
            }
            /* EOF before `sz` (blob shorter than metadata) or a read error: the archive would break Content-Length. */
            EML_ERROR(LOG_TAG, "fd %d: zip blob read short/err (entry %zu, %llu missing)", cli->ctx.fd, i,
                      (unsigned long long)remaining);
            failed = 1;
            break;
        }
        (void)close(fd);
        if(failed)
        {
            db_app_folder_zip_release(h);
            return STATUS_FAILURE;
        }
        off += sz;

        uint8_t dd[ZIP_DD];
        _le32(dd + 0, 0x08074b50u);
        _le32(dd + 4, crc);
        _le64(dd + 8, sz);   /* compressed (8, ZIP64) */
        _le64(dd + 16, sz);  /* uncompressed (8, ZIP64) */
        if(_write_all(cli->ctx.fd, dd, ZIP_DD) != 0)
        {
            db_app_folder_zip_release(h);
            return STATUS_FAILURE;
        }
        off += ZIP_DD;

        db_app_folder_zip_set_result(h, i, crc, entry_off);
    }

    /* 5. Central directory + end-of-central-directory. */
    for(size_t i = 0u; i < n; i++)
    {
        const char* name = NULL;
        size_t      nl   = 0u;
        uint64_t    sz   = 0u;
        uint32_t    crc  = 0u;
        uint64_t    eoff = 0u;
        (void)db_app_folder_zip_entry(h, i, &name, &nl, &sz);
        db_app_folder_zip_result(h, i, &crc, &eoff);

        uint8_t cd[ZIP_CD];
        _le32(cd + 0, 0x02014b50u);
        _le16(cd + 4, 45u);      /* version made by: 4.5 */
        _le16(cd + 6, 45u);      /* version needed: 4.5 (ZIP64) */
        _le16(cd + 8, 0x0808u);  /* flags (match local) */
        _le16(cd + 10, 0u);      /* method */
        _le16(cd + 12, 0u);      /* time */
        _le16(cd + 14, 0x0021u); /* date */
        _le32(cd + 16, crc);
        _le32(cd + 20, 0xFFFFFFFFu); /* comp size → ZIP64 extra */
        _le32(cd + 24, 0xFFFFFFFFu); /* uncomp size → ZIP64 extra */
        _le16(cd + 28, (uint16_t)nl);
        _le16(cd + 30, ZIP_CX);  /* extra len (ZIP64 field) */
        _le16(cd + 32, 0u);      /* comment */
        _le16(cd + 34, 0u);      /* disk start */
        _le16(cd + 36, 0u);      /* internal attrs */
        _le32(cd + 38, 0u);      /* external attrs */
        _le32(cd + 42, 0xFFFFFFFFu); /* local header offset → ZIP64 extra */
        /* central ZIP64 extra: real uncompressed, compressed, and local-header offset (all 8 bytes). */
        uint8_t cx[ZIP_CX];
        _le16(cx + 0, 0x0001u);
        _le16(cx + 2, 24u);      /* data size: uncomp(8) + comp(8) + offset(8) */
        _le64(cx + 4, sz);
        _le64(cx + 12, sz);
        _le64(cx + 20, eoff);
        if(_write_all(cli->ctx.fd, cd, ZIP_CD) != 0 || _write_all(cli->ctx.fd, name, nl) != 0 ||
           _write_all(cli->ctx.fd, cx, ZIP_CX) != 0)
        {
            db_app_folder_zip_release(h);
            return STATUS_FAILURE;
        }
    }

    /* ZIP64 end-of-central-directory record + its locator, then the classic EOCD (whose 32-bit fields are
     * pinned to 0xFFFF/0xFFFFFFFF so every reader consults the ZIP64 records). */
    uint8_t z64[ZIP_Z64EOCD];
    _le32(z64 + 0, 0x06064b50u);
    _le64(z64 + 4, (uint64_t)ZIP_Z64EOCD - 12u); /* size of the remaining record (44) */
    _le16(z64 + 12, 45u);       /* version made by */
    _le16(z64 + 14, 45u);       /* version needed */
    _le32(z64 + 16, 0u);        /* this disk */
    _le32(z64 + 20, 0u);        /* disk with CD start */
    _le64(z64 + 24, (uint64_t)n); /* CD entries this disk */
    _le64(z64 + 32, (uint64_t)n); /* CD entries total */
    _le64(z64 + 40, cd_bytes);
    _le64(z64 + 48, cd_offset);

    uint8_t loc[ZIP_Z64LOC];
    _le32(loc + 0, 0x07064b50u);
    _le32(loc + 4, 0u);         /* disk with the ZIP64 EOCD */
    _le64(loc + 8, z64_eocd);
    _le32(loc + 16, 1u);        /* total disks */

    uint8_t eo[ZIP_EOCD];
    _le32(eo + 0, 0x06054b50u);
    _le16(eo + 4, 0u);          /* this disk */
    _le16(eo + 6, 0u);          /* disk with CD */
    _le16(eo + 8, n < 0xFFFFu ? (uint16_t)n : 0xFFFFu);
    _le16(eo + 10, n < 0xFFFFu ? (uint16_t)n : 0xFFFFu);
    _le32(eo + 12, cd_bytes < 0xFFFFFFFFu ? (uint32_t)cd_bytes : 0xFFFFFFFFu);
    _le32(eo + 16, 0xFFFFFFFFu); /* CD offset → forces readers through the ZIP64 records */
    _le16(eo + 20, 0u);         /* comment len */

    int rc = (_write_all(cli->ctx.fd, z64, ZIP_Z64EOCD) != 0 || _write_all(cli->ctx.fd, loc, ZIP_Z64LOC) != 0 ||
              _write_all(cli->ctx.fd, eo, ZIP_EOCD) != 0)
                 ? -1
                 : 0;

    EML_INFO(LOG_TAG, "fd %d: zip streamed %zu file(s), %llu bytes (%s)", cli->ctx.fd, n, (unsigned long long)total, fname);
    db_app_folder_zip_release(h);
    return (rc == 0) ? STATUS_SUCCESS : STATUS_FAILURE;
}
