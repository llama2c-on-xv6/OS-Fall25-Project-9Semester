/* user/printf.c - compact vprintf/vsnprintf + printf/fprintf wrappers
 *
 * vsnprintf supports: %d %s %f (with .precision) %c %%
 * (keeps stack usage small)
 */

#include "kernel/types.h"
#include "user/user.h"
#include <stdarg.h>
#include <stddef.h>

/* ---- tiny formatting helpers (kept static) ---- */

static void safe_putc_buf(char **outp, size_t *rem, char c) {
    if (*rem > 1) {
        **outp = c;
        (*outp)++;
        (*rem)--;
    }
}
static void safe_puts_buf(char **outp, size_t *rem, const char *s) {
    if (!s) s = "(null)";
    while (*s) safe_putc_buf(outp, rem, *s++);
}

static void itoa_local(long val, char *buf, size_t bufsz) {
    if (bufsz == 0) return;
    char tmp[32];
    int pos = 0;
    unsigned long u;
    int neg = 0;
    if (val < 0) { neg = 1; u = (unsigned long)(-val); } else u = (unsigned long)val;
    if (u == 0) tmp[pos++] = '0';
    while (u > 0) { tmp[pos++] = (char)('0' + (u % 10)); u /= 10; }
    size_t idx = 0;
    if (neg && idx + 1 < bufsz) buf[idx++] = '-';
    for (int i = pos - 1; i >= 0 && idx + 1 < bufsz; --i) buf[idx++] = tmp[i];
    buf[idx < bufsz ? idx : (bufsz-1)] = '\0';
}

static void ftoa_local(double val, int precision, char *out, size_t outsz) {
    if (outsz == 0) return;
    char *p = out;
    size_t rem = outsz;
    if (val < 0) { safe_putc_buf(&p, &rem, '-'); val = -val; }
    long ipart = (long)val;
    double frac = val - (double)ipart;
    char itmp[32]; itoa_local(ipart, itmp, sizeof(itmp));
    safe_puts_buf(&p, &rem, itmp);
    if (precision > 0) {
        safe_putc_buf(&p, &rem, '.');
        double mult = 1.0;
        for (int i = 0; i < precision; ++i) mult *= 10.0;
        double frac_scaled = frac * mult + 0.5; /* simple rounding */
        long fpart = (long)frac_scaled;
        if (fpart >= (long)mult) { fpart -= (long)mult; /* rare carry ignored */ }
        char ftmp[64];
        int pos = 0;
        for (int i = 0; i < precision; ++i) { ftmp[pos++] = '0' + (fpart % 10); fpart /= 10; }
        for (int i = 0; i < pos/2; ++i) { char t = ftmp[i]; ftmp[i] = ftmp[pos-1-i]; ftmp[pos-1-i] = t; }
        ftmp[pos] = '\0';
        safe_puts_buf(&p, &rem, ftmp);
    }
    if (rem > 0) *p = '\0'; else *(p - 1) = '\0';
}

/* ---- vsnprintf implementation (small, safe) ----
 * Accepts bufsz and will not overflow buffer.
 */
int vsnprintf(char *buf, size_t bufsz, const char *fmt, va_list ap) {
    if (bufsz == 0) return 0;
    char *out = buf;
    size_t rem = bufsz;
    const char *p = fmt;

    while (*p) {
        if (*p != '%') { safe_putc_buf(&out, &rem, *p++); continue; }
        p++;
        if (*p == '%') { safe_putc_buf(&out, &rem, '%'); p++; continue; }

        /* optional precision only for %f: "%.4f" */
        int precision = -1;
        if (*p == '.') {
            p++;
            int acc = 0;
            while (*p >= '0' && *p <= '9') { acc = acc * 10 + (*p - '0'); p++; }
            precision = acc;
        }
        char spec = *p++;
        if (spec == 'd') {
            int v = va_arg(ap, int);
            char tmp[48]; itoa_local(v, tmp, sizeof(tmp)); safe_puts_buf(&out, &rem, tmp);
        } else if (spec == 's') {
            const char *sarg = va_arg(ap, const char *);
            if (!sarg) sarg = "(null)";
            safe_puts_buf(&out, &rem, sarg); 
        } else if (spec == 'c') {
            int c = va_arg(ap, int); safe_putc_buf(&out, &rem, (char)c);
        } else if (spec == 'f') {
            double d = va_arg(ap, double);
            int prec = (precision >= 0) ? precision : 6;
            char fbuf[64]; ftoa_local(d, prec, fbuf, sizeof(fbuf)); safe_puts_buf(&out, &rem, fbuf);
        } else {
            safe_putc_buf(&out, &rem, '%');
            if (precision >= 0) safe_putc_buf(&out, &rem, '.');
            safe_putc_buf(&out, &rem, spec);
        }
    }

    if (rem > 0) *out = '\0';
    else buf[bufsz - 1] = '\0';
    return (int)strlen(buf);
}

/* vprintf: small stack buffer (512 bytes) to avoid stack overflow on xv6 */
int vprintf(int fd, const char *fmt, va_list ap) {
    char buf[512];               /* <<<< much smaller than 4096 */
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n < 0) return n;
    size_t towrite = (size_t)n;
    if (towrite >= sizeof(buf)) towrite = sizeof(buf) - 1;
    int w = write(fd, buf, towrite);
    return w < 0 ? w : (int)towrite;
}

int printf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vprintf(1, fmt, ap); /* stdout = 1 */
    va_end(ap);
    return r;
}

int fprintf(int fd, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vprintf(fd, fmt, ap);
    va_end(ap);
    return r;
}

/* fdprintf alias */
int fdprintf(int fd, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vprintf(fd, fmt, ap);
    va_end(ap);
    return r;
}


/* ---- character classification ---- */

int isprint(int c) {
    return (c >= 32 && c <= 126);
}

int isspace(int c) {
    return c == ' ' || c == '\f' || c == '\n' ||
           c == '\r' || c == '\t' || c == '\v';
}

/* ---- parsing helpers (used by vsscanf) ---- */

static const char *parse_int(const char *s, int *out) {
    while (*s && isspace((int)*s)) s++;

    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;

    int val = 0;
    int any = 0;
    while (*s >= '0' && *s <= '9') {
        any = 1;
        val = val * 10 + (*s - '0');
        s++;
    }

    if (!any) return NULL;
    *out = sign * val;
    return s;
}

static const char *parse_double(const char *s, double *out) {
    while (*s && isspace((int)*s)) s++;

    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;

    long ipart = 0;
    int any = 0;
    while (*s >= '0' && *s <= '9') {
        any = 1;
        ipart = ipart * 10 + (*s - '0');
        s++;
    }

    double val = (double)ipart;

    if (*s == '.') {
        s++;
        double place = 0.1;
        while (*s >= '0' && *s <= '9') {
            any = 1;
            val += (*s - '0') * place;
            place *= 0.1;
            s++;
        }
    }

    if (!any) return NULL;
    *out = sign * val;
    return s;
}

/* ---- minimal vsscanf / sscanf ----
 * Supports: %d %s %f %c
 */

int vsscanf(const char *s, const char *fmt, va_list ap) {
    int conversions = 0;
    const char *p = fmt;
    const char *src = s;

    while (*p) {
        if (*p != '%') {
            if (*src == '\0' || *src != *p)
                return conversions;
            src++;
            p++;
            continue;
        }

        p++; /* skip '%' */
        char spec = *p++;

        if (spec == 'd') {
            int *ip = va_arg(ap, int *);
            const char *nxt = parse_int(src, ip);
            if (!nxt) return conversions;
            src = nxt;
            conversions++;
        }
        else if (spec == 's') {
            char *out = va_arg(ap, char *);
            while (*src && !isspace((int)*src))
                *out++ = *src++;
            *out = '\0';
            conversions++;
        }
        else if (spec == 'f') {
            double *dp = va_arg(ap, double *);
            const char *nxt = parse_double(src, dp);
            if (!nxt) return conversions;
            src = nxt;
            conversions++;
        }
        else if (spec == 'c') {
            char *cp = va_arg(ap, char *);
            if (*src == '\0') return conversions;
            *cp = *src++;
            conversions++;
        }
        else {
            return conversions;
        }
    }

    return conversions;
}

int sscanf(const char *s, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsscanf(s, fmt, ap);
    va_end(ap);
    return r;
}

int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, (size_t)-1, fmt, ap);
    va_end(ap);
    return n;
}