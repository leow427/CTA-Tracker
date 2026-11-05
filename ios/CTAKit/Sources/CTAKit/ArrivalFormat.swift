import Foundation

public enum Freshness: Equatable, Sendable { case fresh, dim(minutes: Int), aged(minutes: Int) }

public enum ArrivalFormat {
    /// Whole minutes until `at`, floored and never negative (the firmware's rule).
    public static func minutes(until at: Date, now: Date) -> Int {
        max(0, Int(floor(at.timeIntervalSince(now) / 60)))
    }

    /// "Due" under a minute, "3 min", or for a timetable-only arrival its Chicago clock time ("10:05").
    public static func label(_ p: Prediction, now: Date) -> String {
        guard p.live else { return clock(p.at) }
        let m = minutes(until: p.at, now: now)
        return m < 1 ? "Due" : "\(m) min"
    }

    /// 12-hour Chicago time without AM/PM, whatever the phone's time zone (plan 4 decision 5).
    public static func clock(_ date: Date) -> String {
        let c = CTATime.chicago.dateComponents([.hour, .minute], from: date)
        let h = (c.hour ?? 0) % 12
        return "\(h == 0 ? 12 : h):" + String(format: "%02d", c.minute ?? 0)
    }

    /// Cards dim after 2 min and show their age after 5 (app spec 8).
    public static func freshness(fetched: Date, now: Date) -> Freshness {
        let age = now.timeIntervalSince(fetched)
        if age > Config.staleSec { return .aged(minutes: Int(age / 60)) }
        return age > Config.cardDimSec ? .dim(minutes: Int(age / 60)) : .fresh
    }
}
