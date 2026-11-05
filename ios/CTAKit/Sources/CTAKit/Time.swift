import Foundation

public enum CTATime {
    /// CTA's time zone, for showing clock times.
    public static let chicago: Calendar = {
        var c = Calendar(identifier: .gregorian)
        c.timeZone = TimeZone(identifier: "America/Chicago")!
        return c
    }()

    private static let gmt: Calendar = {
        var c = Calendar(identifier: .gregorian)
        c.timeZone = TimeZone(identifier: "GMT")!
        return c
    }()

    /// Seconds for a CTA wall-clock time read as if it were UTC, like the firmware's parseCtaTime.
    /// Only differences between two such values mean anything; they ignore DST, so the repeated
    /// 1 AM hour and phone-vs-server clock drift can't skew minutes.
    /// Accepts `2026-11-05T08:00:00` (Train Tracker) and `20261105 08:00` or `20261105 08:04:30` (Bus Tracker).
    public static func naive(_ s: String) -> TimeInterval? {
        guard let m = s.wholeMatch(of: /(\d{4})-?(\d{2})-?(\d{2})[T ](\d{2}):(\d{2})(?::(\d{2}))?/) else { return nil }
        let dc = DateComponents(year: Int(m.1), month: Int(m.2), day: Int(m.3), hour: Int(m.4), minute: Int(m.5),
                                second: m.6.flatMap { Int($0) } ?? 0)
        guard dc.isValidDate(in: gmt) else { return nil }
        return gmt.date(from: dc)?.timeIntervalSince1970
    }

    /// When an arrival happens: the fetch time plus how far `time` is after the response's own timestamp.
    static func at(_ time: String, responseTime: String, fetched: Date) -> Date? {
        guard let t = naive(time), let r = naive(responseTime) else { return nil }
        return fetched.addingTimeInterval(t - r)
    }

    /// Night hours are [start, end) in minutes after midnight; they may wrap midnight; start == end means never.
    public static func inNightHours(minuteOfDay m: Int, start: Int, end: Int) -> Bool {
        if start == end { return false }
        return start < end ? (m >= start && m < end) : (m >= start || m < end)
    }
}
