#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include "libc_min.h"

double sqrt(double x) { double r; __asm__("fsqrt" : "=t"(r) : "0"(x)); return r; }
double sin(double x) { double r; __asm__("fsin" : "=t"(r) : "0"(x)); return r; }
double cos(double x) { double r; __asm__("fcos" : "=t"(r) : "0"(x)); return r; }
double tan(double x) { double r; __asm__("fptan\n\tfstp %%st(0)" : "=t"(r) : "0"(x)); return r; }
double atan(double x) { double r; __asm__("fld1\n\tfpatan" : "=t"(r) : "0"(x) : "st(1)"); return r; }
double atan2(double y, double x) { double r; __asm__("fpatan" : "=t"(r) : "0"(x), "u"(y) : "st(1)"); return r; }
double asin(double x) { return atan2(x, sqrt(1.0 - x * x)); }
double acos(double x) { return atan2(sqrt(1.0 - x * x), x); }

static double log2_(double x) { double r; __asm__("fld1\n\tfxch\n\tfyl2x" : "=t"(r) : "0"(x) : "st(1)"); return r; }
double log2(double x) { return log2_(x); }
double log(double x) { double r; __asm__("fldln2\n\tfxch\n\tfyl2x" : "=t"(r) : "0"(x) : "st(1)"); return r; }
double log10(double x) { double r; __asm__("fldlg2\n\tfxch\n\tfyl2x" : "=t"(r) : "0"(x) : "st(1)"); return r; }

static double exp2_(double t) {
    double r;
    __asm__(
        "fld %%st(0)\n\t"
        "frndint\n\t"
        "fsub %%st(0), %%st(1)\n\t"
        "fxch %%st(1)\n\t"
        "f2xm1\n\t"
        "fld1\n\t"
        "faddp %%st(0), %%st(1)\n\t"
        "fscale\n\t"
        "fstp %%st(1)"
        : "=t"(r) : "0"(t) : "st(1)");
    return r;
}

double exp(double x) { return exp2_(x * 1.4426950408889634); }
double expm1(double x) { return exp(x) - 1.0; }
double log1p(double x) { return log(1.0 + x); }

double pow(double x, double y) {
    if (y == 0.0) return 1.0;
    if (x == 0.0) return y > 0.0 ? 0.0 : 1.0 / 0.0;
    if (x < 0.0) {
        const double yi = (double)(long long)y;
        if (yi != y) return 0.0 / 0.0;
        const double m = exp2_(y * log2_(-x));
        return ((long long)y & 1) ? -m : m;
    }
    return exp2_(y * log2_(x));
}

double sinh(double x) { const double e = exp(x); return (e - 1.0 / e) * 0.5; }
double cosh(double x) { const double e = exp(x); return (e + 1.0 / e) * 0.5; }
double tanh(double x) { const double e = exp(2.0 * x); return (e - 1.0) / (e + 1.0); }
double asinh(double x) { return log(x + sqrt(x * x + 1.0)); }
double acosh(double x) { return log(x + sqrt(x * x - 1.0)); }
double atanh(double x) { return 0.5 * log((1.0 + x) / (1.0 - x)); }
double hypot(double x, double y) { return sqrt(x * x + y * y); }

double cbrt(double x) {
    if (x == 0.0) return 0.0;
    const double a = x < 0.0 ? -x : x;
    const double r = exp(log(a) / 3.0);
    return x < 0.0 ? -r : r;
}

double fmod(double x, double y) {
    double r;
    __asm__ volatile(
        "1: fprem\n\t"
        "fnstsw %%ax\n\t"
        "testw $0x400, %%ax\n\t"
        "jnz 1b\n\t"
        "fstp %%st(1)"
        : "=t"(r) : "0"(x), "u"(y) : "ax", "st(1)");
    return r;
}

double scalbn(double x, int n) {
    double r;
    __asm__("fscale" : "=t"(r) : "0"(x), "u"((double)n) : "st(1)");
    return r;
}

double frexp(double x, int* e) {
    if (x == 0.0) {
        *e = 0;
        return 0.0;
    }
    int ex = 0;
    double a = x < 0.0 ? -x : x;
    while (a >= 1.0) {
        a *= 0.5;
        ex++;
    }
    while (a < 0.5) {
        a *= 2.0;
        ex--;
    }
    *e = ex;
    return x < 0.0 ? -a : a;
}

double modf(double x, double* ip) {
    double ipart;
    __asm__("frndint" : "=t"(ipart) : "0"(x));
    if (ipart > x) ipart -= 1.0;
    *ip = ipart;
    return x - ipart;
}

double round(double x) {
    if (x >= 0.0) return (double)(long long)(x + 0.5);
    return -(double)(long long)(-x + 0.5);
}

long lrint(double x) {
    long r;
    __asm__("fistpl %0" : "=m"(r) : "t"(x) : "st");
    return r;
}

double fmax(double a, double b) { return a > b ? a : b; }
double fmin(double a, double b) { return a < b ? a : b; }
int fesetround(int mode) { (void)mode; return 0; }

void* memchr(const void* s, int c, size_t n) {
    const unsigned char* p = s;
    for (size_t i = 0; i < n; i++) {
        if (p[i] == (unsigned char)c) return (void*)(p + i);
    }
    return 0;
}

char* strrchr(const char* s, int c) {
    const char* last = 0;
    for (; *s; s++) {
        if (*s == (char)c) last = s;
    }
    if ((char)c == '\0') return (char*)s;
    return (char*)last;
}

double strtod(const char* s, char** end) {
    double sign = 1.0;
    double v = 0.0;
    int any = 0;
    if (*s == '-') { sign = -1.0; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10.0 + (*s - '0'); s++; any = 1; }
    if (*s == '.') {
        s++;
        double scale = 0.1;
        while (*s >= '0' && *s <= '9') { v += (*s - '0') * scale; scale *= 0.1; s++; any = 1; }
    }
    if (any && (*s == 'e' || *s == 'E')) {
        const char* save = s;
        s++;
        int esign = 1;
        if (*s == '-') { esign = -1; s++; }
        else if (*s == '+') s++;
        if (*s >= '0' && *s <= '9') {
            int e = 0;
            while (*s >= '0' && *s <= '9') { e = e * 10 + (*s - '0'); s++; }
            v = v * pow(10.0, (double)(esign * e));
        } else {
            s = save;
        }
    }
    if (end) *end = (char*)s;
    return sign * v;
}

long strtol(const char* s, char** end, int base) {
    long sign = 1;
    long v = 0;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    if (base == 0) base = 10;
    for (;; s++) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * base + d;
    }
    if (end) *end = (char*)s;
    return sign * v;
}


typedef struct { int tm_sec; int tm_min; int tm_hour; int tm_mday; int tm_mon; int tm_year; int tm_wday; int tm_yday; int tm_isdst; } Tm;
Tm* localtime_r(const long* t, Tm* out) { (void)t; (void)out; return 0; }

void* stdout = 0;
int fputc(int c, void* f) { (void)f; return c; }
int putc(int c, void* f) { (void)f; return c; }

void abort(void) {
    for (;;) {
    }
}

int pthread_once(int* once, void (*fn)(void)) {
    if (*once == 0) {
        *once = 1;
        fn();
    }
    return 0;
}

int pthread_mutex_init(void* m, const void* a) { (void)m; (void)a; return 0; }
int pthread_mutex_destroy(void* m) { (void)m; return 0; }
int pthread_mutex_lock(void* m) { (void)m; return 0; }
int pthread_mutex_unlock(void* m) { (void)m; return 0; }
int pthread_cond_init(void* c, const void* a) { (void)c; (void)a; return 0; }
int pthread_cond_destroy(void* c) { (void)c; return 0; }
int pthread_cond_wait(void* c, void* m) { (void)c; (void)m; return 0; }
int pthread_cond_timedwait(void* c, void* m, const void* t) { (void)c; (void)m; (void)t; return 0; }
int pthread_cond_signal(void* c) { (void)c; return 0; }
int pthread_cond_broadcast(void* c) { (void)c; return 0; }
int pthread_condattr_init(void* a) { (void)a; return 0; }
int pthread_condattr_destroy(void* a) { (void)a; return 0; }
int pthread_condattr_setclock(void* a, int c) { (void)a; (void)c; return 0; }

double fabs(double x) {
    return x < 0.0 ? -x : x;
}

static int in_range(double x) {
    return x == x && x < 9.0e18 && x > -9.0e18;
}

double trunc(double x) {
    if (!in_range(x)) return x;
    return (double)(long long)x;
}

double floor(double x) {
    if (!in_range(x)) return x;
    double t = (double)(long long)x;
    if (t > x) t -= 1.0;
    return t;
}

double ceil(double x) {
    if (!in_range(x)) return x;
    double t = (double)(long long)x;
    if (t < x) t += 1.0;
    return t;
}

double copysign(double x, double y) {
    const double a = fabs(x);
    return (y < 0.0 || (y == 0.0 && 1.0 / y < 0.0)) ? -a : a;
}

int atoi(const char* s) {
    return (int)strtol(s, 0, 10);
}

int fprintf(void* f, const char* fmt, ...) {
    (void)f;
    (void)fmt;
    return 0;
}

int vfprintf(void* f, const char* fmt, va_list ap) {
    (void)f;
    (void)fmt;
    (void)ap;
    return 0;
}

void __assert_fail(const char* expr, const char* file, unsigned int line, const char* func) {
    (void)expr;
    (void)file;
    (void)line;
    (void)func;
    for (;;) {
    }
}