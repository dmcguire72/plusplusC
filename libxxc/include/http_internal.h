// ++C Runtime Library (xxCRT)
// Copyright 2026 Daniel McGuire
// Licensed under the MIT License

#ifndef HTTP_INTERNAL_H
#define HTTP_INTERNAL_H

#include <http.pph>
#include <alloc.h>
#include <mem.h>
#include <str.h>
#include <cio.h>
#include <clock.h>

static inline u8 http__lower(u8 c)
{
    return (c >= 'A' && c <= 'Z') ? (u8)(c + ('a' - 'A')) : c;
}

static inline bool http__is_digit(u8 c)
{
    return c >= '0' && c <= '9';
}

static inline int http__hexval(u8 c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static inline bool http__is_token_char(u8 c)
{
    if (c >= '0' && c <= '9') return true;
    if (c >= 'a' && c <= 'z') return true;
    if (c >= 'A' && c <= 'Z') return true;
    switch (c)
    {
        case '!': case '#': case '$': case '%': case '&': case '\'':
        case '*': case '+': case '-': case '.': case '^': case '_':
        case '`': case '|': case '~':
            return true;
        default:
            return false;
    }
}

static inline bool http__is_ows(u8 c)
{
    return c == ' ' || c == '\t';
}

static inline bool http__valid_value_char(u8 c)
{
    return !((c < 0x20 && c != '\t') || c == 0x7f);
}

static inline bool http__ieq_n(const char *s, size_t n, const char *b)
{
    size_t i = 0;
    for (; i < n; i++)
    {
        if (b[i] == '\0' || http__lower((u8)s[i]) != http__lower((u8)b[i]))
            return false;
    }
    return b[i] == '\0';
}

static inline bool http__ieq(const char *s, const char *b)
{
    return http__ieq_n(s, strlen(s), b);
}

static inline bool http__list_has(const char *list, const char *token)
{
    if (list == nullptr)
        return false;

    size_t tlen = strlen(token);
    const char *p = list;

    for (;;)
    {
        while (*p == ' ' || *p == '\t' || *p == ',')
            p++;
        if (*p == '\0')
            return false;

        const char *start = p;
        while (*p != '\0' && *p != ',')
            p++;

        const char *end = p;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
            end--;

        if ((size_t)(end - start) == tlen && http__ieq_n(start, tlen, token))
            return true;
    }
}

static inline bool http__parse_u64(const char *s, size_t n, u64 *out)
{
    if (n == 0)
        return false;

    u64 v = 0;
    for (size_t i = 0; i < n; i++)
    {
        if (!http__is_digit((u8)s[i]))
            return false;
        u64 d = (u64)(s[i] - '0');
        if (v > (0xFFFFFFFFFFFFFFFFull - d) / 10ull)
            return false;
        v = v * 10ull + d;
    }

    *out = v;
    return true;
}

static inline void *http__memchr(const void *p, int c, size_t n)
{
    const u8 *b = (const u8 *)p;
    for (size_t i = 0; i < n; i++)
        if (b[i] == (u8)c)
            return (void *)(b + i);
    return nullptr;
}

static inline char *http__strdup_n(const char *s, size_t n)
{
    char *d = (char *)malloc(n + 1);
    if (d == nullptr)
        return nullptr;
    memcpy(d, s, n);
    d[n] = '\0';
    return d;
}

typedef struct {
    u8    *data;
    size_t len;
    size_t cap;
    bool   oom;
} http_buf_t;

static inline bool http_buf_reserve(http_buf_t *b, size_t extra)
{
    if (b->oom)
        return false;

    if (extra > (size_t)-1 - b->len - 1)
    {
        b->oom = true;
        return false;
    }

    size_t need = b->len + extra + 1;
    if (need <= b->cap)
        return true;

    size_t cap = b->cap ? b->cap : 256;
    while (cap < need)
    {
        if (cap > ((size_t)-1) / 2) { cap = need; break; }
        cap *= 2;
    }

    u8 *p = (u8 *)realloc(b->data, cap);
    if (p == nullptr)
    {
        b->oom = true;
        return false;
    }

    b->data = p;
    b->cap  = cap;
    return true;
}

static inline bool http_buf_append(http_buf_t *b, const void *data, size_t n)
{
    if (n == 0)
        return !b->oom;
    if (!http_buf_reserve(b, n))
        return false;
    memcpy(b->data + b->len, data, n);
    b->len += n;
    b->data[b->len] = 0;
    return true;
}

static inline bool http_buf_puts(http_buf_t *b, const char *s)
{
    return http_buf_append(b, s, strlen(s));
}

static inline bool http_buf_putc(http_buf_t *b, char c)
{
    return http_buf_append(b, &c, 1);
}

static inline bool http_buf_put_u64(http_buf_t *b, u64 v)
{
    char tmp[24];
    int i = 0;
    if (v == 0)
        tmp[i++] = '0';
    while (v > 0)
    {
        tmp[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    char out[24];
    int o = 0;
    while (i > 0)
        out[o++] = tmp[--i];
    return http_buf_append(b, out, (size_t)o);
}

static inline bool http_buf_put_hex(http_buf_t *b, u64 v)
{
    static const char digits[] = "0123456789abcdef";
    char tmp[20];
    int i = 0;
    if (v == 0)
        tmp[i++] = '0';
    while (v > 0)
    {
        tmp[i++] = digits[v & 0xF];
        v >>= 4;
    }
    char out[20];
    int o = 0;
    while (i > 0)
        out[o++] = tmp[--i];
    return http_buf_append(b, out, (size_t)o);
}

static inline void http_buf_free(http_buf_t *b)
{
    free(b->data);
    b->data = nullptr;
    b->len = b->cap = 0;
    b->oom = false;
}

enum {
    HTTP_LINE_OK = 0,
    HTTP_LINE_EOF,
    HTTP_LINE_ERR,
    HTTP_LINE_TOO_LONG
};

enum {
    HTTP_PARSE_OK      = 0,
    HTTP_PARSE_EOF     = -1,
    HTTP_PARSE_IOERR   = -2,
    HTTP_PARSE_TIMEOUT = -3,
    HTTP_PARSE_CANCEL  = -4
};

typedef enum {
    HTTP_BODY_NONE = 0,
    HTTP_BODY_LENGTH,
    HTTP_BODY_CHUNKED,
    HTTP_BODY_EOF
} http_body_kind;

int  http__next_line(HTTPConnection *c, char **line, size_t *len, size_t max_len);
bool http__wait_data(HTTPConnection *c);
bool http__read_exact(HTTPConnection *c, void *dst, size_t n);
int  http__fail_code(const HTTPConnection *c);
int  http__read_headers(HTTPConnection *c, Map *headers, size_t *budget);
int  http__body_framing(Map *headers, bool is_request, int version_minor,
                        http_body_kind *kind, u64 *length);
int  http__read_body(HTTPConnection *c, http_body_kind kind, u64 length, size_t max_body,
                     u8 **out, size_t *out_len);
Map *http__new_string_map(void);

static inline const char *http__map_get(Map *m, const char *key)
{
    return m ? (const char *)map_get_cstr(&m->table, key) : nullptr;
}

void conn_set_rcv(HTTPConnection *c, u32 want_ms);
i64 conn_read_raw(HTTPConnection *c, void *dst, size_t n);
bool conn_make_room(HTTPConnection *c, size_t limit);
i64 conn_recv(HTTPConnection *c);

#endif /* HTTP_INTERNAL_H */
