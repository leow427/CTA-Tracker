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

int64_t floorDiv(int64_t a, int64_t b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }

// The proleptic Gregorian year of a day count since 1970-01-01 (from Howard Hinnant's civil_from_days).
int yearOfDays(int64_t z) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    const int64_t m = mp < 10 ? mp + 3 : mp - 9;
    return static_cast<int>(yoe + era * 400 + (m <= 2));
}

// The day count of the nth Sunday of a month. 1970-01-01 was a Thursday.
int64_t nthSunday(int y, int m, int n) {
    const int64_t first = daysFromCivil(y, m, 1);
    const int64_t weekday = (first + 4) - floorDiv(first + 4, 7) * 7;  // 0 = Sunday
    return first + (7 - weekday) % 7 + 7 * (n - 1);
}

constexpr int64_t kHour = 3600;
constexpr int64_t kCst = 6 * kHour;  // UTC = Chicago wall time + 6 h in standard time
constexpr int64_t kCdt = 5 * kHour;  // + 5 h in daylight time

// The UTC instants a Chicago wall time (naive seconds) can mean: one, or two in the fall-back night's repeated
// hour (daylight reading first).
int utcReadings(int64_t wall, int64_t out[2]) {
    const int y = yearOfDays(floorDiv(wall, 86400));
    const int64_t springGap = nthSunday(y, 3, 2) * 86400 + 2 * kHour;  // 2:00 CST becomes 3:00 CDT
    const int64_t fallRepeat = nthSunday(y, 11, 1) * 86400 + 1 * kHour;  // 1:00–1:59 happens twice
    if (wall < springGap + kHour) {
        out[0] = wall + kCst;  // before the jump, or the skipped hour read as standard time
        return 1;
    }
    if (wall < fallRepeat) {
        out[0] = wall + kCdt;
        return 1;
    }
    if (wall < fallRepeat + kHour) {
        out[0] = wall + kCdt;
        out[1] = wall + kCst;
        return 2;
    }
    out[0] = wall + kCst;
    return 1;
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

bool ctaSecondsBetween(const char* from, const char* to, int64_t& secs) {
    int64_t a, b;
    if (!parseCtaTime(from, a) || !parseCtaTime(to, b)) return false;
    int64_t ra[2], rb[2];
    const int na = utcReadings(a, ra), nb = utcReadings(b, rb);
    // In the repeated hour the readings are exactly an hour apart, so one window of an hour picks one: the
    // difference nearest kExpectedGap, i.e. (−5, +55] min, where real arrivals fall. Ties go to the later one.
    constexpr int64_t kExpectedGap = 25 * 60;
    bool found = false;
    int64_t best = 0;
    for (int i = 0; i < na; ++i) {
        for (int j = 0; j < nb; ++j) {
            const int64_t d = rb[j] - ra[i];
            const int64_t off = d - kExpectedGap, bestOff = best - kExpectedGap;
            const int64_t ad = off < 0 ? -off : off, abest = bestOff < 0 ? -bestOff : bestOff;
            if (!found || ad < abest || (ad == abest && d > best)) {
                best = d;
                found = true;
            }
        }
    }
    secs = best;
    return true;
}

bool inNightHours(int minuteOfDay, int startMin, int endMin) {
    if (startMin == endMin) return false;
    if (startMin < endMin) return minuteOfDay >= startMin && minuteOfDay < endMin;
    return minuteOfDay >= startMin || minuteOfDay < endMin;  // wraps past midnight
}

}  // namespace cta
