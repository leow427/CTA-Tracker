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

/// The card list redraws only at `ArrivalFormat.nextChange`, so nothing a card shows, and not the cards' order, may
/// change between two of them: each change then shows the moment it happens, where the old 15 s clock showed it up to
/// 15 s late.
struct ListNextChangeTests {
    // Just before night hours end (5:00 Chicago), so the emptied bus card's "no service now" flips during the run.
    let now = chicago("2026-11-05T04:52:00")
    let red = Line(route: "Red", dir: "Howard"), redS = Line(route: "Red", dir: "95th/Dan Ryan")
    let n22 = Line(route: "22", dir: "Northbound")
    var slots: [Slot] {
        [Slot(line: red, stopID: "30255", fetchID: "41320", kind: .station, meters: 300),
         Slot(line: redS, stopID: "30256", fetchID: "41320", kind: .station, meters: 300),
         Slot(line: n22, stopID: "1001", fetchID: "1001", kind: .bus, meters: 300)]   // same distance: order by arrival
    }

    func p(_ stop: String, _ route: String, _ seconds: Double, dir: String = "", live: Bool = true) -> Prediction {
        Prediction(stopID: stop, route: route, dir: dir, dest: "", at: now.addingTimeInterval(seconds), live: live, delayed: false)
    }

    /// Everything the list shows that depends on time: the order, and each card's arrivals, "also", freshness,
    /// spoken sentence, and night hours.
    func shown(_ preds: [Prediction], fetched: [String: Date], at t: Date) -> String {
        let c = CTATime.chicago.dateComponents([.hour, .minute], from: t)
        let night = CTATime.inNightHours(minuteOfDay: (c.hour ?? 0) * 60 + (c.minute ?? 0),
                                         start: Config.nightStartMin, end: Config.nightEndMin)
        let order = CardOrder.sorted(slots, predictions: preds, now: t).map(\.id)
        let cards = slots.map { slot in
            let mine = slot.predictions(from: preds, now: t)
            let freshness = fetched[slot.fetchID].map { ArrivalFormat.freshness(fetched: $0, now: t) } ?? .fresh
            return [mine.prefix(3).map { ArrivalFormat.label($0, now: t) }.joined(separator: "/"),   // next and 2 later
                    ArrivalFormat.also(Array(mine.dropFirst().prefix(2)), now: t) ?? "", "\(freshness)",
                    CardSpeech.sentence(line: slot.line, kind: slot.kind, stopName: "", next: mine.first,
                                        later: Array(mine.dropFirst().prefix(2)), alert: false, freshness: freshness,
                                        now: t, night: night)].joined(separator: "|")
        }
        return "\(order)\n" + cards.map { "\($0) night:\(night)" }.joined(separator: "\n")
    }

    /// Steps from `now` in `step`s for `seconds`, following `ticks`, and checks `key` never changes between two
    /// ticks: every change then shows at its tick, within a millisecond of happening, where the 15 s clock showed it up
    /// to 15 s late. Returns the number of ticks.
    func expectStill(for seconds: Double, step: Double = 0.05, ticks: (Date) -> Date?, _ key: (Date) -> String) -> Int {
        var t = now.addingTimeInterval(0.123)
        var next = ticks(t) ?? .distantFuture
        var since = key(t)
        var count = 0
        while t < now.addingTimeInterval(seconds) {
            t = t.addingTimeInterval(step)
            while t >= next {   // two changes can fall within one step
                since = key(next)
                let after = ticks(next) ?? .distantFuture
                #expect(after > next)
                next = after
                count += 1
            }
            #expect(key(t) == since, "at \(t.timeIntervalSince(now))")
        }
        return count
    }

    /// Each card on its own clock: nothing it shows changes between its ticks.
    @Test func eachCardStillBetweenItsTicks() {
        let preds = [p("30255", "Red", 20.5), p("30255", "Red", 125), p("30255", "Red", 413.3), p("30255", "Red", 1_300),
                     p("30256", "Red", 61), p("30256", "Red", 900, live: false), p("30256", "Red", -45),
                     p("1001", "22", 119.999, dir: "Northbound"), p("1001", "22", 300, dir: "Northbound")]
        let fetched = ["41320": now.addingTimeInterval(-37.3), "1001": now.addingTimeInterval(-250)]
        for (n, slot) in slots.enumerated() {
            let mine = [preds.filter(slot.matches)]
            let ticks = expectStill(for: 1_500, ticks: {
                ArrivalFormat.nextChange(after: $0, cards: mine, shownPerCard: 3, fetched: fetched[slot.fetchID].map { [$0] } ?? [])
            }) { t in shown(preds, fetched: fetched, at: t).card(n) }
            #expect(ticks < 100)   // the 15 s clock's 100
        }
    }

    /// The list's own clock follows only the order, which changes when a soonest arrival drops off between cards at the
    /// same distance; checked with many cards.
    @Test func orderStillBetweenItsTicks() {
        // 60 cards over 20 distances: three lines per stop, arrivals spread over 25 minutes.
        let many = (0..<60).map { n in
            Slot(line: Line(route: "R\(n % 3)", dir: "Northbound"), stopID: "s\(n / 3)", fetchID: "s\(n / 3)", kind: .bus,
                 meters: Double(100 * (n / 3)))
        }
        let preds = many.enumerated().flatMap { n, slot in
            (0..<3).map { k in
                Prediction(stopID: slot.stopID, route: slot.line.route, dir: "Northbound", dest: "",
                           at: now.addingTimeInterval(Double((n * 37 + k * 211) % 1_400) - 50.5), live: k != 2, delayed: false)
            }
        }
        let ticks = expectStill(for: 1_500, step: 0.25, ticks: { CardOrder.nextChange(after: $0, many, predictions: preds) }) { t in
            CardOrder.sorted(many, predictions: preds, now: t).map(\.id).joined(separator: ",")
        }
        #expect(ticks > 0 && ticks <= preds.count)   // at most one per drop-off
        // Cards alone at their distance never reorder.
        let apart = many.enumerated().map { n, s in
            Slot(line: s.line, stopID: s.stopID, fetchID: s.fetchID, kind: s.kind, meters: Double(n))
        }
        #expect(CardOrder.nextChange(after: now, apart, predictions: preds) == nil)
    }

    @Test func dataAgeCountsUpEachMinute() {
        let fetched = now.addingTimeInterval(-301)   // aged 5 min; 6 at 360 s
        let next = ArrivalFormat.nextChange(after: now, cards: [], shownPerCard: 3, fetched: [fetched])
        #expect(abs(next.timeIntervalSince(fetched) - 360) < 0.01)
        #expect(ArrivalFormat.freshness(fetched: fetched, now: next) == .aged(minutes: 6))
        // Stop detail shows no age, so only its staleness counts there.
        let stop = Stop(id: "1", name: "", lat: 0, lon: 0, kind: .bus, lines: [], platforms: nil)
        #expect(DetailGroups.nextChange(after: now, stop: stop, predictions: [], fetched: fetched) > next)
    }
}

private extension String {
    /// Card `n`'s line of a `ListNextChangeTests.shown` key (line 0 is the order).
    func card(_ n: Int) -> String { String(split(separator: "\n")[n + 1]) }
}
