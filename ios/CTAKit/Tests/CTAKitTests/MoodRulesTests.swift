import Foundation
import Testing
@testable import CTAKit

// Mirrors firmware test_mood (plan 1, task 7): one case per row of base spec 6.3.
struct MoodRulesTests {
    let now = Date(timeIntervalSince1970: 10_000)

    func rec(_ count: Int, _ min0: Int = 5, live: Bool = true, delayed: Bool = false,
             alert: Bool = false, ts: Int = 9_990) -> StopRecord {
        let first = Arrival(min: min0, dest: "Howard", live: live, delayed: delayed)
        let rest = Arrival(min: min0 + 10, dest: "Howard", live: true, delayed: false)
        return StopRecord(id: "30255", type: .train, route: "Red", dir: "", name: "Belmont",
                          arrivals: Array(([first] + Array(repeating: rest, count: 3)).prefix(count)),
                          alert: alert, ts: ts)
    }

    func stop(_ r: StopRecord, night: Bool = false) -> Mood { MoodRules.stopMood(r, now: now, night: night) }
    func device(_ recs: [StopRecord], canHype: Bool = true, hasLocation: Bool = true) -> Mood {
        MoodRules.deviceMood(recs.map { ($0, canHype) }, now: now, night: false, hasLocation: hasLocation)
    }

    let happy: StopRecord
    let worried: StopRecord
    let sad: StopRecord
    let sleepy: StopRecord
    init() {
        happy = StopRecord(id: "1", type: .bus, route: "22", dir: "", name: "", arrivals: [Arrival(min: 9, dest: "", live: true, delayed: false)], alert: false, ts: 9_990)
        worried = StopRecord(id: "2", type: .bus, route: "22", dir: "", name: "", arrivals: [Arrival(min: 9, dest: "", live: true, delayed: true)], alert: false, ts: 9_990)
        sad = StopRecord(id: "3", type: .bus, route: "22", dir: "", name: "", arrivals: [], alert: false, ts: 9_990)
        sleepy = StopRecord(id: "4", type: .bus, route: "22", dir: "", name: "", arrivals: [], alert: false, ts: 0)
    }

    @Test func stopMoods() {
        #expect(stop(rec(1, ts: 0)) == .sleepy)                  // never fetched
        #expect(stop(rec(1, ts: 10_000 - 301)) == .sleepy)       // stale
        #expect(stop(rec(1, ts: 10_000 - 300)) == .happy)        // exactly 5 min is not stale
        #expect(stop(rec(1), night: true) == .sleepy)            // night
        #expect(stop(rec(1, alert: true)) == .sad)               // alert
        #expect(stop(rec(0)) == .sad)                            // no predictions
        #expect(stop(rec(1, delayed: true)) == .worried)
        #expect(stop(rec(1)) == .happy)
        #expect(stop(rec(1, live: false)) == .happy)             // scheduled only (plan 1 decision 2)
    }

    @Test func recordFromAnOldFetchIsSleepy() {
        let fetched = Date(timeIntervalSince1970: 10_000)
        let p = Prediction(stopID: "1", route: "22", dir: "", dest: "", at: fetched.addingTimeInterval(120), live: true, delayed: false)
        let r = StopRecord.make(id: "1", kind: .bus, name: "", from: [p], alert: false, fetched: fetched)
        #expect(r.ts == 10_000)
        #expect(MoodRules.stopMood(r, now: fetched.addingTimeInterval(360), night: false) == .sleepy)
    }

    @Test func deviceMoods() {
        #expect(device([happy, happy, happy], hasLocation: false) == .sleepy)   // no location
        #expect(MoodRules.deviceMood([(happy, true)], now: now, night: true, hasLocation: true) == .sleepy)
        #expect(device([rec(1, 3)]) == .hyped)
        #expect(device([rec(1, 4)]) != .hyped)
        #expect(device([rec(1, 3, live: false)]) != .hyped)                    // timetable can't hype
        #expect(device([rec(1, 1)], canHype: false) != .hyped)                 // far stop can't hype
        #expect(device([rec(1, 1, ts: 10_000 - 301)]) != .hyped)               // stale can't hype (decision 4)
        #expect(device([happy, happy, worried]) == .happy)
        #expect(device([happy, worried]) == .worried)                          // tie → Worried
        #expect(device([sad, worried]) == .sad)                                // tie → Sad
        #expect(device([sleepy, sleepy, worried]) == .worried)                 // Sleepy stops excluded
        #expect(device([sleepy, sleepy]) == .sleepy)
        #expect(device([]) == .sleepy)
    }
}
