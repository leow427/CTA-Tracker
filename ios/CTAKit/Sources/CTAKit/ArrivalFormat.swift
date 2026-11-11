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

    /// The first moment after `now` when anything shown about these arrivals can read differently, so a view can
    /// redraw only then instead of on a fixed clock. `cards` holds each card's (or detail group's) arrivals, of which
    /// the soonest `shownPerCard` still in the grace period show. The changes: a shown arrival's whole minutes tick
    /// down or it drops off (which can also reorder the list), data fetched at `fetched` dims, turns stale, or (with
    /// `ageMinutes`) its shown age in minutes grows, or night hours begin or end. Each lands a hair past its boundary,
    /// since the rules compare strictly, so every change shows within a millisecond of when it happens.
    public static func nextChange(after now: Date, cards: [[Prediction]], shownPerCard: Int, fetched: [Date],
                                  ageMinutes: Bool = true) -> Date {
        let t = now.timeIntervalSinceReferenceDate
        var next = nightChange(after: now).timeIntervalSinceReferenceDate
        func consider(_ boundary: TimeInterval) {
            let at = boundary + changeLag
            if at > t { next = min(next, at) }
        }
        for card in cards {
            // A later arrival can't come into view before an earlier one drops off, which is itself a tick.
            let shown = card.filter { $0.at.timeIntervalSince(now) > -Config.departedGraceSec }
                .sorted { $0.at < $1.at }.prefix(shownPerCard)
            for p in shown {
                let at = p.at.timeIntervalSinceReferenceDate
                let m = minutes(until: p.at, now: now)
                if p.live, m >= 1 { consider(at - Double(m) * 60) }   // one minute fewer (scheduled ones show a clock time)
                consider(at + Config.departedGraceSec)        // drops off
            }
        }
        for f in fetched {
            let at = f.timeIntervalSinceReferenceDate
            consider(at + Config.cardDimSec)
            guard ageMinutes else { continue }
            consider(at + Config.staleSec)
            // The age shows from 2 min (dimmed) and counts up each whole minute: 3, 4, ...
            consider(at + max(3, floor((t - at) / 60) + 1) * 60)
        }
        return Date(timeIntervalSinceReferenceDate: next)
    }

    /// When night hours (base spec 6.3, Chicago time) next begin or end.
    static func nightChange(after now: Date) -> Date {
        [Config.nightStartMin, Config.nightEndMin].compactMap {
            CTATime.chicago.nextDate(after: now, matching: DateComponents(hour: $0 / 60, minute: $0 % 60, second: 0),
                                     matchingPolicy: .nextTime)
        }.min() ?? now.addingTimeInterval(24 * 3600)
    }

    /// How far past a boundary `nextChange` lands, so the strict comparisons have flipped.
    static let changeLag: TimeInterval = 0.001
}
