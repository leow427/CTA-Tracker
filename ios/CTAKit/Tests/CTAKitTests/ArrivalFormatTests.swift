import Foundation
import Testing
@testable import CTAKit

struct ArrivalFormatTests {
    let now = chicago("2026-11-05T08:00:00")

    func live(_ seconds: TimeInterval) -> Prediction {
        Prediction(stopID: "1", route: "22", dir: "", dest: "", at: now.addingTimeInterval(seconds), live: true, delayed: false)
    }

    @Test func liveLabels() {
        #expect(ArrivalFormat.label(live(0), now: now) == "Due")
        #expect(ArrivalFormat.label(live(59), now: now) == "Due")      // under 1 minute
        #expect(ArrivalFormat.label(live(-30), now: now) == "Due")     // already passed
        #expect(ArrivalFormat.label(live(60), now: now) == "1 min")
        #expect(ArrivalFormat.label(live(12 * 60 + 30), now: now) == "12 min")
    }

    @Test func scheduledShowsChicagoClockTimeInAnyZone() {
        let saved = NSTimeZone.default
        NSTimeZone.default = TimeZone(identifier: "America/Los_Angeles")!
        defer { NSTimeZone.default = saved }
        let p = Prediction(stopID: "1", route: "Red", dir: "", dest: "", at: chicago("2026-11-05T10:05:00"), live: false, delayed: false)
        #expect(ArrivalFormat.label(p, now: now) == "10:05")
        let pm = Prediction(stopID: "1", route: "Red", dir: "", dest: "", at: chicago("2026-11-05T12:40:00"), live: false, delayed: false)
        #expect(ArrivalFormat.label(pm, now: now) == "12:40")
    }

    @Test(arguments: [
        (119.0, Freshness.fresh),
        (121.0, .dim(minutes: 2)),
        (299.0, .dim(minutes: 4)),
        (301.0, .aged(minutes: 5)),
        (3_600.0, .aged(minutes: 60)),
    ])
    func freshness(age: TimeInterval, expected: Freshness) {
        #expect(ArrivalFormat.freshness(fetched: now, now: now.addingTimeInterval(age)) == expected)
    }
}
