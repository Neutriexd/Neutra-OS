#ifndef NEUTRA_HTTP_H
#define NEUTRA_HTTP_H

#include <stdint.h>

typedef struct {
    int status;
    char location[160];
} HttpResponse;

int http_fetch(const char* host, const char* path, uint8_t* body, int body_max, HttpResponse* res);
int https_fetch(const char* host, const char* path, uint8_t* body, int body_max, HttpResponse* res);
int http_request(int https, const char* host, const char* path, const char* method, const uint8_t* req_body,
                 int req_len, uint8_t* body, int body_max, HttpResponse* res);
void http_set_progress(void (*fn)(const char*));
void http_cmd(const char* text);

#endif
