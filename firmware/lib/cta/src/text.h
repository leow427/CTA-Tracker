#pragma once
#include <stddef.h>

namespace cta {
// Copies at most maxBytes bytes of src, never ending inside a UTF-8 sequence; always NUL-terminates
// (dst needs maxBytes + 1). A null src gives "".
void copyTruncated(char* dst, const char* src, size_t maxBytes);
}  // namespace cta
