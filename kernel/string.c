#include <horizon/string.h>

void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else if (d > s) {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    while (n--) {
        *d++ = (uint8_t)c;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    for (size_t i = 0; i < n; i++) {
        if (pa[i] != pb[i]) return (int)pa[i] - (int)pb[i];
    }
    return 0;
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

size_t strnlen(const char *s, size_t maxlen) {
    size_t len = 0;
    while (len < maxlen && s[len]) len++;
    return len;
}

int strcmp(const char *a, const char *b) {
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i] || a[i] == '\0') {
            return (int)(uint8_t)a[i] - (int)(uint8_t)b[i];
        }
    }
    return 0;
}

char *strcpy(char *dst, const char *src) {
    char *orig = dst;
    while ((*dst++ = *src++)) {}
    return orig;
}

char *strncpy(char *dst, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == '\0') ? (char *)s : NULL;
}

char *strrchr(const char *s, int c) {
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if (c == '\0') return (char *)s;
    return (char *)last;
}

typedef struct {
    char  *buf;
    size_t cap;
    size_t pos;
} fmt_out_t;

static void out_ch(fmt_out_t *o, char c) {
    if (o->cap > 0 && o->pos + 1 < o->cap) {
        o->buf[o->pos] = c;
    }
    o->pos++;
}

static void out_num(fmt_out_t *o, uint64_t val, uint32_t base, bool neg,
                    bool upper, int width, char pad, bool left) {
    char tmp[32];
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int idx = 0;
    if (val == 0) {
        tmp[idx++] = '0';
    } else {
        while (val > 0) {
            tmp[idx++] = digits[val % base];
            val /= base;
        }
    }

    int total = idx + (neg ? 1 : 0);
    if (!left) {
        if (neg && pad == '0') out_ch(o, '-');
        for (int i = total; i < width; i++) out_ch(o, pad);
        if (neg && pad != '0') out_ch(o, '-');
    } else if (neg) {
        out_ch(o, '-');
    }

    while (idx > 0) out_ch(o, tmp[--idx]);

    if (left) {
        for (int i = total; i < width; i++) out_ch(o, ' ');
    }
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    fmt_out_t o = { buf, size, 0 };

    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            out_ch(&o, *p);
            continue;
        }
        p++;
        char pad = ' ';
        bool left = false;
        for (;;) {
            if (*p == '-') {
                left = true;
                p++;
            } else if (*p == '0') {
                pad = '0';
                p++;
            } else {
                break;
            }
        }
        if (left) pad = ' ';
        int width = 0;
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }
        int long_cnt = 0;
        if (*p == 'z') {
            long_cnt = 2;
            p++;
        } else {
            while (*p == 'l') {
                long_cnt++;
                p++;
            }
        }
        switch (*p) {
            case '%':
                out_ch(&o, '%');
                break;
            case 'c':
                out_ch(&o, (char)va_arg(ap, int));
                break;
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                int len = (int)strlen(s);
                if (!left) {
                    for (int i = len; i < width; i++) out_ch(&o, ' ');
                }
                while (*s) out_ch(&o, *s++);
                if (left) {
                    for (int i = len; i < width; i++) out_ch(&o, ' ');
                }
                break;
            }
            case 'd':
            case 'i': {
                int64_t v = (long_cnt >= 2) ? va_arg(ap, int64_t)
                          : (long_cnt == 1) ? va_arg(ap, long)
                                            : va_arg(ap, int);
                bool neg = v < 0;
                uint64_t uv = neg ? (uint64_t)(-(v + 1)) + 1ULL : (uint64_t)v;
                out_num(&o, uv, 10, neg, false, width, pad, left);
                break;
            }
            case 'u': {
                uint64_t v = (long_cnt >= 2) ? va_arg(ap, uint64_t)
                           : (long_cnt == 1) ? (uint64_t)va_arg(ap, unsigned long)
                                             : (uint64_t)va_arg(ap, unsigned int);
                out_num(&o, v, 10, false, false, width, pad, left);
                break;
            }
            case 'x':
            case 'X': {
                uint64_t v = (long_cnt >= 2) ? va_arg(ap, uint64_t)
                           : (long_cnt == 1) ? (uint64_t)va_arg(ap, unsigned long)
                                             : (uint64_t)va_arg(ap, unsigned int);
                out_num(&o, v, 16, false, (*p == 'X'), width, pad, left);
                break;
            }
            case 'p': {
                uint64_t v = (uint64_t)(uintptr_t)va_arg(ap, void *);
                out_ch(&o, '0');
                out_ch(&o, 'x');
                out_num(&o, v, 16, false, false, 16, '0', false);
                break;
            }
            default:
                out_ch(&o, '%');
                if (*p) out_ch(&o, *p);
                break;
        }
    }

    if (o.cap > 0) {
        size_t term = (o.pos < o.cap) ? o.pos : (o.cap - 1);
        o.buf[term] = '\0';
    }
    return (int)o.pos;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}
