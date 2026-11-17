import CTAKit
import Foundation
import Testing
@testable import CTATracker

// The positions poll (F6, app spec 5.2): one fetch per interval, it stops when the page does, and a failing feed or a
// focus that drops out reads honestly. A fake feed and a short interval keep it fast.
struct LiveVehiclesTests {
    final class Feed {
        var times: [Date] = []
        var calls: Int { times.count }
        var replies: [[Vehicle]?] = [[]]   // one per fetch; the last repeats
        func next() async -> [Vehicle]? {
            times.append(.now)
            await Task.yield()   // a network fetch suspends; a poll that never waits would spin here, not hang the test
            return replies[min(calls, replies.count) - 1]
        }
    }

    final class Ended { var yes = false }

    /// Brown run 411, Loop-bound for Wellington, as ttpositions sends it.
    static let train = try! TrainParse.positions(Data(#"""
        {"ctatt":{"errCd":"0","route":[{"train":[{"rn":"411","destNm":"Loop","nextStpId":"30232",
        "nextStaNm":"Wellington","lat":"41.94","lon":"-87.65","heading":"180"}]}]}}
        """#.utf8))[0]

    func live(_ feed: Feed, interval: TimeInterval = 0.2, keepSec: TimeInterval = 90) -> LiveVehicles {
        LiveVehicles(interval: interval, keepSec: keepSec) { _, _ in await feed.next() }
    }

    func start(_ live: LiveVehicles, route: String = "Brn", target: String = "30236") -> (Task<Void, Never>, Ended) {
        let ended = Ended()
        let task = Task { await live.poll(route: route, kind: .station, target: target, index: nil); ended.yes = true }
        return (task, ended)
    }

    /// Waits until `done` holds, at most `seconds`; whether it did.
    func until(_ seconds: Double = 2, _ done: () -> Bool) async -> Bool {
        let end = Date.now.addingTimeInterval(seconds)
        while !done() {
            if Date.now > end { return false }
            try? await Task.sleep(for: .milliseconds(10))
        }
        return true
    }

    @Test func fetchesOncePerInterval() async {
        let feed = Feed()
        let (task, _) = start(live(feed, interval: 0.25))
        #expect(await until { feed.calls >= 3 })
        task.cancel()
        // Bus Tracker allows 10,000 calls a day: a poll that forgot when it last fetched would spend them in minutes.
        let gaps = zip(feed.times, feed.times.dropFirst()).map { $1.timeIntervalSince($0) }
        #expect(gaps.allSatisfy { $0 >= 0.24 }, "gaps \(gaps)")
    }

    @Test func endsWhenTheStopCloses() async {
        let feed = Feed()
        let model = live(feed)
        let (task, ended) = start(model)
        #expect(await until { feed.calls == 1 })
        model.reset()
        #expect(await until { ended.yes })
        #expect(feed.calls == 1)
        task.cancel()
    }

    @Test func endsWhenAnotherLineTakesOver() async {
        let feed = Feed()
        let model = live(feed)
        let (brown, ended) = start(model)
        #expect(await until { feed.calls == 1 })
        let (red, redEnded) = start(model, route: "Red", target: "30256")
        #expect(await until { ended.yes })
        #expect(model.route == "Red" && !redEnded.yes)
        red.cancel()
        brown.cancel()
    }

    @Test func endsWhenCancelled() async {
        let feed = Feed()
        let (task, ended) = start(live(feed))
        #expect(await until { feed.calls == 1 })
        task.cancel()
        #expect(await until { ended.yes })
        #expect(feed.calls == 1)
    }

    @Test func aFailingFeedKeepsItsPositionsOnlyAWhile() async {
        let feed = Feed()
        feed.replies = [[Self.train], nil]
        let model = live(feed, interval: 0.1, keepSec: 0.35)
        let (task, _) = start(model)
        #expect(await until { model.failing })
        #expect(model.vehicles == [Self.train])   // 0.1 s old: still shown
        #expect(await until { model.vehicles == nil })
        #expect(model.failing)
        task.cancel()
    }

    @Test func aFocusThatDropsOutReadsAsLost() async {
        let feed = Feed()
        feed.replies = [[Self.train], []]
        let model = live(feed, interval: 0.15)
        let (task, _) = start(model)
        #expect(await until { model.vehicles != nil })
        model.toggleFocus("411")
        #expect(model.focusStatus == .found(Self.train))
        #expect(await until { model.vehicles == [] })
        #expect(model.focusStatus == .lost)
        task.cancel()
    }

    @Test func anotherStopOnTheLineDropsTheFocus() async {
        let feed = Feed()
        feed.replies = [[Self.train]]
        let model = live(feed)
        var (task, _) = start(model)
        #expect(await until { model.vehicles != nil })
        model.toggleFocus("411")
        // The app coming back to the foreground restarts the poll for the same stop: the focus stays.
        task.cancel()
        (task, _) = start(model)
        #expect(await until { feed.calls >= 2 })
        #expect(model.focus == "411")
        // Another stop on the same line: the focus goes, so the camera stays at the new stop.
        task.cancel()
        (task, _) = start(model, target: "30128")
        #expect(await until { model.focus == nil })
        #expect(!model.seen)
        task.cancel()
    }
}
