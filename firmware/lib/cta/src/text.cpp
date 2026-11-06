#include "text.h"

#include <string.h>

namespace cta {

void copyTruncated(char* dst, const char* src, size_t maxBytes) {
    if (!src) src = "";
    size_t n = strnlen(src, maxBytes + 1);
    if (n > maxBytes) {
        n = maxBytes;
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--;   // src[n] continues a cut sequence
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

}  // namespace cta
