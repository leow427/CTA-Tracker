#include "cta_fetch.h"

#include <HTTPClient.h>
#include <NetworkClientSecure.h>

// The Mozilla CA bundle that ESP-IDF links into mbedTLS (CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL).
extern const uint8_t kCaBundleStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t kCaBundleEnd[] asm("_binary_x509_crt_bundle_end");

namespace cta {

int ctaGet(const char* url, char* buf, size_t cap, uint32_t timeoutMs) {
    if (!buf || cap < 2) return -3;
    buf[0] = '\0';
    NetworkClientSecure tls;
    tls.setCACertBundle(kCaBundleStart, kCaBundleEnd - kCaBundleStart);
    tls.setHandshakeTimeout((timeoutMs + 999) / 1000);   // seconds; the default lets a stalled handshake block 120 s
    HTTPClient http;
    http.useHTTP10(true);   // no chunked encoding: the body streams as-is until the server closes
    http.setTimeout(timeoutMs);
    http.setConnectTimeout(timeoutMs);
    http.setReuse(false);
    if (!http.begin(tls, url)) return -1;
    int status = http.GET();
    if (status <= 0) {
        http.end();
        return -1;
    }
    if (status != 200) {
        http.end();
        return -2;
    }
    NetworkClient* stream = http.getStreamPtr();
    size_t n = 0;
    uint32_t start = millis();
    int result = 0;
    while (millis() - start < timeoutMs) {
        int avail = stream->available();
        if (avail > 0) {
            if (n + avail > cap - 1) {
                result = -3;
                break;
            }
            n += stream->readBytes(buf + n, avail);
        } else if (!stream->connected()) {
            break;
        } else {
            delay(2);
        }
    }
    if (!result && millis() - start >= timeoutMs) result = -1;
    http.end();
    if (result) {
        buf[0] = '\0';
        return result;
    }
    buf[n] = '\0';
    return (int)n;
}

}  // namespace cta
