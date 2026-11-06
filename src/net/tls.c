#include <stddef.h>
#include <stdint.h>
#include "tls.h"
#include "tcp.h"
#include "entropy.h"
#include "kernel_memory.h"
#include "libc_min.h"
#include "mbedtls/ssl.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/error.h"

void memcpy(void* dst, const void* src, size_t n);

#define CA_BUNDLE "kernel_memory/cacert.pem"
#define CA_BUNDLE_ALT "cacert.pem"
#define IDLE_LIMIT 3000000
#define TLS_ERR_SEND -0x004E
#define TLS_ERR_RECV -0x004C

static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context drbg;
static mbedtls_x509_crt cacert;
static mbedtls_ssl_config conf;
static mbedtls_ssl_context ssl;
static int setup_done;
static int setup_failed;
static int connected;
static int last_error;
static void (*tls_progress)(const char*);

void tls_set_progress(void (*fn)(const char*)) {
    tls_progress = fn;
}

static void tls_report(const char* s) {
    if (tls_progress) tls_progress(s);
}
static int setup_error_code;

static int bio_send(void* ctx, const unsigned char* buf, size_t len) {
    (void)ctx;
    const int r = tcp_send(buf, (int)len);
    return r < 0 ? TLS_ERR_SEND : (int)len;
}

static int bio_recv(void* ctx, unsigned char* buf, size_t len) {
    (void)ctx;
    const int r = tcp_read(buf, (int)len, IDLE_LIMIT);
    if (r > 0) return r;
    if (r == 0) return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
    return TLS_ERR_RECV;
}

static int load_trust_store(void) {
    const uint8_t* data = 0;
    uint32_t size = 0;
    if (!kernel_memory_get_file(CA_BUNDLE, &data, &size) && !kernel_memory_get_file(CA_BUNDLE_ALT, &data, &size)) return -1;
    if (size == 0) return -1;
    unsigned char* copy = calloc(1, size + 1);
    if (!copy) return -1;
    memcpy(copy, data, size);
    copy[size] = 0;
    const int r = mbedtls_x509_crt_parse(&cacert, copy, size + 1);
    free(copy);
    return r < 0 ? -1 : 0;
}

static int setup_once(void) {
    if (setup_done) {
        if (setup_failed) last_error = setup_error_code;
        return setup_failed ? -1 : 0;
    }
    setup_done = 1;
    if (!entropy_has_rdrand()) {
        setup_failed = 1;
        setup_error_code = -100;
        last_error = -100;
        return -1;
    }
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);
    mbedtls_x509_crt_init(&cacert);
    mbedtls_ssl_config_init(&conf);
    mbedtls_ssl_init(&ssl);
    const unsigned char pers[] = "neutra-tls";
    if (mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, pers, sizeof(pers) - 1) != 0) {
        setup_failed = 1;
        setup_error_code = -101;
        last_error = -101;
        return -1;
    }
    if (mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
        setup_failed = 1;
        setup_error_code = -102;
        last_error = -102;
        return -1;
    }
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    if (load_trust_store() != 0) {
        setup_failed = 1;
        setup_error_code = -103;
        last_error = -103;
        return -1;
    }
    mbedtls_ssl_conf_ca_chain(&conf, &cacert, 0);
    return 0;
}

int tls_last_error(void) {
    return last_error;
}

int tls_open(const uint8_t ip[4], uint16_t port, const char* host) {
    last_error = 0;
    if (setup_once() != 0) return -1;
    if (connected) tls_close();
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_init(&ssl);
    tls_report("TCP connect (443)...");
    if (tcp_connect(ip, port) != 0) {
        last_error = -10;
        return -2;
    }
    if (mbedtls_ssl_setup(&ssl, &conf) != 0) {
        tcp_close();
        last_error = -11;
        return -3;
    }
    mbedtls_ssl_set_hostname(&ssl, host);
    mbedtls_ssl_set_bio(&ssl, 0, bio_send, bio_recv, 0);
    tls_report("TLS handshake...");
    int r;
    int tries = 0;
    do {
        r = mbedtls_ssl_handshake(&ssl);
    } while ((r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) && ++tries < 2000);
    if (r != 0) {
        last_error = r;
        mbedtls_ssl_free(&ssl);
        tcp_close();
        return -4;
    }
    connected = 1;
    tls_report("TLS ready");
    return 0;
}

int tls_write(const uint8_t* data, int len) {
    if (!connected) return -1;
    int done = 0;
    while (done < len) {
        const int r = mbedtls_ssl_write(&ssl, data + done, (size_t)(len - done));
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (r <= 0) {
            last_error = r;
            return -1;
        }
        done += r;
    }
    return done;
}

int tls_read(uint8_t* out, int max) {
    if (!connected) return -1;
    for (;;) {
        const int r = mbedtls_ssl_read(&ssl, out, (size_t)max);
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (r > 0) return r;
        if (r == 0 || r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
        last_error = r;
        return -1;
    }
}

void tls_close(void) {
    if (!connected) return;
    mbedtls_ssl_close_notify(&ssl);
    tcp_close();
    connected = 0;
}