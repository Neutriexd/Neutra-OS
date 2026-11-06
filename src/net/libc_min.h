    #ifndef NEUTRA_LIBC_MIN_H
#define NEUTRA_LIBC_MIN_H

#include <stdarg.h>
#include <stddef.h>

void* calloc(size_t count, size_t size);
void free(void* ptr);
void* memmove(void* dst, const void* src, size_t n);
int memcmp(const void* a, const void* b, size_t n);
size_t strlen(const char* s);
char* strchr(const char* s, int c);
char* strstr(const char* hay, const char* needle);
int snprintf(char* out, size_t max, const char* fmt, ...);
int vsnprintf(char* out, size_t max, const char* fmt, va_list ap);

#endif