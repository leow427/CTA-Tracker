#include "text.h"

#include <string.h>

namespace cta {

void copyTruncated(char* dst, const char* src, size_t maxBytes) {
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    size_t n = 0;
    while (n <= maxBytes && src[n] != '\0') ++n;  // stop one past the limit: is there more?
    if (n > maxBytes) {
        n = maxBytes;
        while (n > 0 && (static_cast<unsigned char>(src[n]) & 0xC0) == 0x80) --n;  // src[n] continues a cut sequence
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

}  // namespace cta
