#include "local_time.h"

#include <stddef.h>

namespace cta {

namespace {

// Reads exactly n ASCII digits at s; false if any isn't a digit.
bool digits(const char* s, int n, int& out) {
    int v = 0;
    for (int i = 0; i < n; ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (s[i] - '0');
    }
    out = v;
    return true;
}

bool isLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int daysInMonth(int y, int m) {
    static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return (m == 2 && isLeap(y)) ? 29 : kDays[m - 1];
}

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's days_from_civil).
int64_t daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

size_t length(const char* s) {
    size_t n = 0;
    while (s[n]) ++n;
    return n;
}

}  // namespace

bool parseCtaTime(const char* s, int64_t& secs) {
    if (s == nullptr) return false;
    const size_t n = length(s);
    int y, mo, d, h, mi, sec = 0;
    if (n == 19 && s[4] == '-' && s[7] == '-' && s[10] == 'T' && s[13] == ':' && s[16] == ':') {
        // YYYY-MM-DDTHH:MM:SS
        if (!digits(s, 4, y) || !digits(s + 5, 2, mo) || !digits(s + 8, 2, d) || !digits(s + 11, 2, h) ||
            !digits(s + 14, 2, mi) || !digits(s + 17, 2, sec))
            return false;
    } else if ((n == 14 || n == 17) && s[8] == ' ' && s[11] == ':' && (n == 14 || s[14] == ':')) {
        // YYYYMMDD HH:MM[:SS]
        if (!digits(s, 4, y) || !digits(s + 4, 2, mo) || !digits(s + 6, 2, d) || !digits(s + 9, 2, h) ||
            !digits(s + 12, 2, mi))
            return false;
        if (n == 17 && !digits(s + 15, 2, sec)) return false;
    } else {
        return false;
    }
    if (mo < 1 || mo > 12 || d < 1 || d > daysInMonth(y, mo) || h > 23 || mi > 59 || sec > 59) return false;
    secs = daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + sec;
    return true;
}

bool inNightHours(int minuteOfDay, int startMin, int endMin) {
    if (startMin == endMin) return false;
    if (startMin < endMin) return minuteOfDay >= startMin && minuteOfDay < endMin;
    return minuteOfDay >= startMin || minuteOfDay < endMin;  // wraps past midnight
}

}  // namespace cta
