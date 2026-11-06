#include "local_time.h"

#include <ctype.h>

namespace cta {

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's days_from_civil).
static int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static bool digits(const char*& p, int n, int& out) {
    out = 0;
    for (int i = 0; i < n; i++, p++) {
        if (!isdigit((unsigned char)*p)) return false;
        out = out * 10 + (*p - '0');
    }
    return true;
}

bool parseCtaTime(const char* s, int64_t& secs) {
    if (!s) return false;
    const char* p = s;
    int y, mo, d, h, mi, se = 0;
    if (!digits(p, 4, y)) return false;
    if (*p == '-') p++;
    if (!digits(p, 2, mo)) return false;
    if (*p == '-') p++;
    if (!digits(p, 2, d)) return false;
    if (*p != 'T' && *p != ' ') return false;
    p++;
    if (!digits(p, 2, h) || *p++ != ':' || !digits(p, 2, mi)) return false;
    if (*p == ':') {
        p++;
        if (!digits(p, 2, se)) return false;
    }
    if (*p != '\0') return false;
    static const int kDays[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (mo < 1 || mo > 12 || d < 1 || d > kDays[mo - 1] || h > 23 || mi > 59 || se > 59) return false;
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    if (mo == 2 && d == 29 && !leap) return false;
    secs = daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + se;
    return true;
}

bool inNightHours(int m, int start, int end) {
    if (start == end) return false;
    return start < end ? (m >= start && m < end) : (m >= start || m < end);
}

}  // namespace cta
