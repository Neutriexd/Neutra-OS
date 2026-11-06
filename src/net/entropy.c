#include <stddef.h>
#include <stdint.h>
#include "entropy.h"

int mbedtls_hardware_poll(void* data, unsigned char* output, size_t len, size_t* olen);

static int rdrand_known = -1;

static int detect_rdrand(void) {
    uint32_t eax = 1;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    return (int)((ecx >> 30) & 1);
}

int entropy_has_rdrand(void) {
    if (rdrand_known < 0) rdrand_known = detect_rdrand();
    return rdrand_known;
}

static int rdrand32(uint32_t* value) {
    for (int attempt = 0; attempt < 10; attempt++) {
        unsigned char ok;
        uint32_t v;
        __asm__ volatile("rdrand %0\n\tsetc %1" : "=r"(v), "=qm"(ok));
        if (ok) {
            *value = v;
            return 1;
        }
    }
    return 0;
}

int mbedtls_hardware_poll(void* data, unsigned char* output, size_t len, size_t* olen) {
    (void)data;
    size_t got = 0;
    if (!entropy_has_rdrand()) {
        *olen = 0;
        return -1;
    }
    while (got < len) {
        uint32_t v;
        if (!rdrand32(&v)) {
            *olen = got;
            return -1;
        }
        for (int i = 0; i < 4 && got < len; i++) output[got++] = (unsigned char)(v >> (8 * i));
    }
    *olen = got;
    return 0;
}