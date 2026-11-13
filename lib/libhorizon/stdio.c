#include <stdbool.h>

#include <horizon/libhorizon.h>

#define PRINTF_BUF_SIZE 256

typedef struct {
    char    buf[PRINTF_BUF_SIZE];
    size_t  len;
    int     total;
    int64_t error;
} printf_ctx_t;

static void ctx_flush(printf_ctx_t *ctx) {
    if (ctx->len == 0) return;
    if (ctx->error == 0) {
        int64_t rc = hnt_write(HNT_STDOUT, ctx->buf, ctx->len);
        if (rc < 0) ctx->error = rc;
    }
    ctx->len = 0;
}

static void ctx_putc(printf_ctx_t *ctx, char c) {
    if (ctx->len == sizeof(ctx->buf)) ctx_flush(ctx);
    ctx->buf[ctx->len++] = c;
    ctx->total++;
}

static void ctx_puts(printf_ctx_t *ctx, const char *s) {
    while (*s) ctx_putc(ctx, *s++);
}

static void ctx_repeat(printf_ctx_t *ctx, char c, int count) {
    while (count-- > 0) ctx_putc(ctx, c);
}

static void ctx_number(printf_ctx_t *ctx, uint64_t value, unsigned base,
                       bool upper, bool negative, int width, bool left,
                       bool zero) {
    char tmp[32];
    int n = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";

    do {
        tmp[n++] = digits[value % base];
        value /= base;
    } while (value);

    int prefix = negative ? 1 : 0;
    int pad = width > n + prefix ? width - (n + prefix) : 0;

    if (negative && (!zero || left)) ctx_putc(ctx, '-');
    if (!left && !zero) ctx_repeat(ctx, ' ', pad);
    if (negative && zero && !left) ctx_putc(ctx, '-');
    if (!left && zero) ctx_repeat(ctx, '0', pad);

    while (n > 0) ctx_putc(ctx, tmp[--n]);

    if (left) ctx_repeat(ctx, ' ', pad);
}

static void ctx_string(printf_ctx_t *ctx, const char *s, int width, bool left) {
    if (!s) s = "(null)";
    int len = (int)strlen(s);
    int pad = width > len ? width - len : 0;

    if (!left) ctx_repeat(ctx, ' ', pad);
    ctx_puts(ctx, s);
    if (left) ctx_repeat(ctx, ' ', pad);
}

int hnt_vprintf(const char *fmt, va_list ap) {
    printf_ctx_t ctx;
    ctx.len = 0;
    ctx.total = 0;
    ctx.error = 0;

    const char *p = fmt;
    while (*p) {
        if (*p != '%') {
            ctx_putc(&ctx, *p++);
            continue;
        }
        p++;
        if (*p == '\0') break;

        bool left = false;
        bool zero = false;
        while (*p == '-' || *p == '0') {
            if (*p == '-') left = true;
            else zero = true;
            p++;
        }

        int width = 0;
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }

        int length = 0; /* 0=int, 1=long, 2=long long, 3=size_t */
        while (*p == 'l') {
            length++;
            p++;
        }
        if (*p == 'z') {
            length = 3;
            p++;
        }

        char spec = *p;
        if (spec == '\0') break;
        p++;

        switch (spec) {
            case '%':
                ctx_putc(&ctx, '%');
                break;
            case 'c': {
                char c = (char)va_arg(ap, int);
                int pad = width > 1 ? width - 1 : 0;
                if (!left) ctx_repeat(&ctx, ' ', pad);
                ctx_putc(&ctx, c);
                if (left) ctx_repeat(&ctx, ' ', pad);
                break;
            }
            case 's':
                ctx_string(&ctx, va_arg(ap, const char *), width, left);
                break;
            case 'd':
            case 'i': {
                int64_t sv = (length >= 1) ? va_arg(ap, int64_t)
                                           : (int64_t)va_arg(ap, int);
                bool negative = sv < 0;
                uint64_t mag = negative ? (uint64_t)(-(sv + 1)) + 1
                                        : (uint64_t)sv;
                ctx_number(&ctx, mag, 10, false, negative, width, left, zero);
                break;
            }
            case 'u':
            case 'x':
            case 'X': {
                uint64_t uv = (length >= 1) ? va_arg(ap, uint64_t)
                                            : (uint64_t)va_arg(ap, unsigned int);
                ctx_number(&ctx, uv, (spec == 'u') ? 10 : 16,
                           spec == 'X', false, width, left, zero);
                break;
            }
            case 'p': {
                uint64_t value = (uint64_t)va_arg(ap, void *);
                ctx_puts(&ctx, "0x");
                ctx_number(&ctx, value, 16, false, false, 16, false, true);
                break;
            }
            default:
                ctx_putc(&ctx, '%');
                ctx_putc(&ctx, spec);
                break;
        }
    }

    ctx_flush(&ctx);
    return ctx.error ? (int)ctx.error : ctx.total;
}

int hnt_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = hnt_vprintf(fmt, ap);
    va_end(ap);
    return n;
}
