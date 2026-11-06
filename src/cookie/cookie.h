#ifndef NEUTRA_COOKIE_H
#define NEUTRA_COOKIE_H

void cookie_store(const char* host, const char* headers, int hlen);
int cookie_header(const char* host, const char* path, char* out, int max);

#endif
