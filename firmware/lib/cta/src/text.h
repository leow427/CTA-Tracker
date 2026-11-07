#pragma once
// UTF-8-safe fixed-size string copies (plan 1, decision 3). Mirrored by the app's String.truncatedUTF8.
#include <stddef.h>

namespace cta {

// Copies the longest prefix of src that fits in maxBytes without ending inside a UTF-8 sequence, then a NUL.
// dst must hold maxBytes + 1 bytes. A null src copies "".
void copyTruncated(char* dst, const char* src, size_t maxBytes);

}  // namespace cta
