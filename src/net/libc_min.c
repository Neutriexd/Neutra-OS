#include <stdarg.h>
#include <stdint.h>
#include "libc_min.h"

#define HEAP_SIZE (16 * 1024 * 1024)
#define BLOCK_HDR 16
#define BIG_LIMIT 65536
#define BIN_COUNT 13

static uint8_t heap[HEAP_SIZE] __attribute__((aligned(16)));
static size_t heap_top;
static void* bins[BIN_COUNT];
static void* big_list;

static int bin_of(size_t cap) {
    int k = 0;
    size_t c = 16;
    while (c < cap) {
        c <<= 1;
        k++;
    }
    return k;
}

static void hdr_write(uint8_t* user, size_t cap) {
    uint8_t* h = user - BLOCK_HDR;
    h[0] = (uint8_t)cap;
    h[1] = (uint8_t)(cap >> 8);
    h[2] = (uint8_t)(cap >> 16);
    h[3] = (uint8_t)(cap >> 24);
}

static size_t hdr_read(const uint8_t* user) {
    const uint8_t* h = user - BLOCK_HDR;
    return (size_t)h[0] | ((size_t)h[1] << 8) | ((size_t)h[2] << 16) | ((size_t)h[3] << 24);
}

static uint8_t* fresh_block(size_t cap) {
    const size_t need = BLOCK_HDR + cap;
    if (heap_top + need > HEAP_SIZE) return 0;
    uint8_t* user = heap + heap_top + BLOCK_HDR;
    heap_top += need;
    hdr_write(user, cap);
    return user;
}

static uint8_t* take_block(size_t cap) {
    if (cap <= BIG_LIMIT) {
        const int k = bin_of(cap);
        if (bins[k]) {
            void* user = bins[k];
            bins[k] = *(void**)user;
            return (uint8_t*)user;
        }
        return fresh_block((size_t)16 << k);
    }
    void** prev = &big_list;
    for (void* cur = big_list; cur; cur = *(void**)cur) {
        if (hdr_read(cur) >= cap) {
            *prev = *(void**)cur;
            return (uint8_t*)cur;
        }
        prev = (void**)cur;
    }
    return fresh_block(cap);
}

void* calloc(size_t count, size_t size) {
    const size_t total = count * size;
    if (count != 0 && total / count != size) return 0;
    size_t cap;
    if (total <= BIG_LIMIT) {
        cap = (size_t)16 << bin_of(total ? total : 1);
    } else {
        cap = (total + 15) & ~(size_t)15;
    }
    uint8_t* user = take_block(cap);
    if (!user) return 0;
    for (size_t i = 0; i < total; i++) user[i] = 0;
    return user;
}

void free(void* ptr) {
    if (!ptr) return;
    const size_t cap = hdr_read(ptr);
    if (cap <= BIG_LIMIT) {
        const int k = bin_of(cap);
        *(void**)ptr = bins[k];
        bins[k] = ptr;
    } else {
        *(void**)ptr = big_list;
        big_list = ptr;
    }
}

void* malloc(size_t size) {
    return calloc(1, size);
}

void* realloc(void* ptr, size_t size) {
    if (!ptr) return malloc(size);
    const size_t cap = hdr_read(ptr);
    if (cap >= size) return ptr;
    void* fresh = malloc(size);
    if (!fresh) return 0;
    uint8_t* d = fresh;
    const uint8_t* src = ptr;
    for (size_t i = 0; i < cap; i++) d[i] = src[i];
    free(ptr);
    return fresh;
}

size_t malloc_usable_size(void* ptr) {
    if (!ptr) return 0;
    return hdr_read(ptr);
}

void* memmove(void* dst, const void* src, size_t n) {
    uint8_t* d = dst;
    const uint8_t* s = src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dst;
}


int memcmp(const void* a, const void* b, size_t n) {
    const uint8_t* x = a;
    const uint8_t* y = b;
    for (size_t i = 0; i < n; i++) {
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    }
    return 0;
}

size_t strlen(const char* s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}



char* strchr(const char* s, int c) {
    for (;; s++) {
        if (*s == (char)c) return (char*)s;
        if (*s == '\0') return 0;
    }
}

char* strstr(const char* hay, const char* needle) {
    const size_t m = strlen(needle);
    if (m == 0) return (char*)hay;
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < m && hay[i] == needle[i]) i++;
        if (i == m) return (char*)hay;
    }
    return 0;
}

static int put(char* out, size_t max, size_t* pos, char c) {
    if (*pos + 1 < max) out[*pos] = c;
    (*pos)++;
    return 0;
}

static void put_str(char* out, size_t max, size_t* pos, const char* s) {
    while (*s) put(out, max, pos, *s++);
}

static void put_unsigned(char* out, size_t max, size_t* pos, unsigned long value, unsigned base, int upper) {
    char digits[32];
    int n = 0;
    const char* set = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do {
        digits[n++] = set[value % base];
        value /= base;
    } while (value && n < 31);
    while (n > 0) put(out, max, pos, digits[--n]);
}

int vsnprintf(char* out, size_t max, const char* fmt, va_list ap) {
    size_t pos = 0;
    for (const char* f = fmt; *f; f++) {
        if (*f != '%') {
            put(out, max, &pos, *f);
            continue;
        }
        f++;
        while (*f == '-' || *f == '0' || *f == '.' || (*f >= '1' && *f <= '9')) f++;
        int is_long = 0;
        while (*f == 'l' || *f == 'z' || *f == 'h') {
            if (*f == 'l' || *f == 'z') is_long = 1;
            f++;
        }
        switch (*f) {
            case 'd': {
                long v = is_long ? va_arg(ap, long) : (long)va_arg(ap, int);
                if (v < 0) {
                    put(out, max, &pos, '-');
                    put_unsigned(out, max, &pos, (unsigned long)(-v), 10, 0);
                } else {
                    put_unsigned(out, max, &pos, (unsigned long)v, 10, 0);
                }
                break;
            }
            case 'u':
                put_unsigned(out, max, &pos, is_long ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int), 10, 0);
                break;
            case 'x':
            case 'X':
                put_unsigned(out, max, &pos, is_long ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int), 16, *f == 'X');
                break;
            case 's': {
                const char* s = va_arg(ap, const char*);
                put_str(out, max, &pos, s ? s : "(null)");
                break;
            }
            case 'c':
                put(out, max, &pos, (char)va_arg(ap, int));
                break;
            case '%':
                put(out, max, &pos, '%');
                break;
            default:
                break;
        }
    }
    if (max > 0) out[pos < max ? pos : max - 1] = '\0';
    return (int)pos;
}

int snprintf(char* out, size_t max, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(out, max, fmt, ap);
    va_end(ap);
    return n;
}

int printf(const char* fmt, ...) {
    (void)fmt;
    return 0;
}

void explicit_bzero(void* p, size_t n) {
    uint8_t* d = p;
    for (size_t i = 0; i < n; i++) d[i] = 0;
}

void __stack_chk_fail(void) {
    for (;;) {
    }
}

int* __errno_location(void) {
    static int errno_value;
    return &errno_value;
}

int putchar(int c) { return c; }
int puts(const char* s) { (void)s; return 0; }
long time(long* t) { if (t) *t = 0; return 0; }
int rand(void) { return 0; }
void* fopen(const char* p, const char* m) { (void)p; (void)m; return 0; }
int fclose(void* f) { (void)f; return -1; }
size_t fread(void* p, size_t s, size_t n, void* f) { (void)p; (void)s; (void)n; (void)f; return 0; }
size_t fwrite(const void* p, size_t s, size_t n, void* f) { (void)p; (void)s; (void)n; (void)f; return 0; }
int fseek(void* f, long o, int w) { (void)f; (void)o; (void)w; return -1; }
long ftell(void* f) { (void)f; return -1; }
int ferror(void* f) { (void)f; return 1; }
char* fgets(char* s, int n, void* f) { (void)s; (void)n; (void)f; return 0; }
void setbuf(void* f, char* b) { (void)f; (void)b; }
int remove(const char* p) { (void)p; return -1; }
int rename(const char* a, const char* b) { (void)a; (void)b; return -1; }
int stat(const char* p, void* st) { (void)p; (void)st; return -1; }
void* opendir(const char* p) { (void)p; return 0; }
void* readdir(void* d) { (void)d; return 0; }
int closedir(void* d) { (void)d; return -1; }
void* gmtime_r(const long* t, void* out) { (void)t; (void)out; return 0; }

static unsigned long fake_ticks;

static void fake_now(long* sec, long* frac, long frac_scale) {
    fake_ticks++;
    const unsigned long us = fake_ticks * 1000UL;
    *sec = (long)(us / 1000000UL);
    *frac = (long)((us % 1000000UL) * (unsigned long)frac_scale);
}

int clock_gettime(int id, void* ts) {
    (void)id;
    if (!ts) return -1;
    long* p = ts;
    fake_now(&p[0], &p[1], 1000L);
    return 0;
}

int gettimeofday(void* tv, void* tz) {
    (void)tz;
    if (!tv) return -1;
    long* p = tv;
    fake_now(&p[0], &p[1], 1L);
    return 0;
}
int inet_pton(int af, const char* s, void* out) { (void)af; (void)s; (void)out; return -1; }
int socket(int a, int b, int c) { (void)a; (void)b; (void)c; return -1; }
int bind(int a, const void* b, unsigned c) { (void)a; (void)b; (void)c; return -1; }
int connect(int a, const void* b, unsigned c) { (void)a; (void)b; (void)c; return -1; }
int listen(int a, int b) { (void)a; (void)b; return -1; }
int close(int a) { (void)a; return -1; }
int read(int a, void* b, size_t c) { (void)a; (void)b; (void)c; return -1; }
int write(int a, const void* b, size_t c) { (void)a; (void)b; (void)c; return -1; }
int fcntl(int a, int b, ...) { (void)a; (void)b; return -1; }
int shutdown(int a, int b) { (void)a; (void)b; return -1; }
int getsockopt(int a, int b, int c, void* d, void* e) { (void)a; (void)b; (void)c; (void)d; (void)e; return -1; }
int setsockopt(int a, int b, int c, const void* d, unsigned e) { (void)a; (void)b; (void)c; (void)d; (void)e; return -1; }
int getsockname(int a, void* b, void* c) { (void)a; (void)b; (void)c; return -1; }
int getaddrinfo(const char* a, const char* b, const void* c, void* d) { (void)a; (void)b; (void)c; (void)d; return -1; }
void freeaddrinfo(void* a) { (void)a; }
int select(int a, void* b, void* c, void* d, void* e) { (void)a; (void)b; (void)c; (void)d; (void)e; return -1; }
long recvfrom(int a, void* b, size_t c, int d, void* e, void* f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return -1; }
long syscall(long n, ...) { (void)n; return -1; }

char* strncpy(char* dst, const char* src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i] != '\0'; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}

int accept(int fd, void* addr, void* len) {
    (void)fd;
    (void)addr;
    (void)len;
    return -1;
}

void* __sysv_signal(int sig, void* handler) {
    (void)sig;
    (void)handler;
    return 0;
}

void exit(int status) {
    (void)status;
    for (;;) {
    }
}

int mbedtls_psa_platform_get_builtin_key(uint32_t key_id_value, uint32_t key_id_owner,
                                         uint32_t* lifetime, uint32_t* slot_number) {
    (void)key_id_value;
    (void)key_id_owner;
    (void)lifetime;
    (void)slot_number;
    return -140;
}