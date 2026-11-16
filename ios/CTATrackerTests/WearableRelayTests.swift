import CTAKit
import Foundation
import Testing
import UIKit
@testable import CTATracker

// The relay's decisions, without a radio (plan Task AB3).
struct WearableRelayTests {
    let index: StopIndex
    let fetched = Date(timeIntervalSince1970: 1_762_700_000)

    init() throws {
        let url = try #require(Bundle.main.url(forResource: "stops", withExtension: "json"))
        index = try StopIndex(data: Data(contentsOf: url))
    }

    func fixture(_ name: String) throws -> Data {
        try Data(contentsOf: URL(filePath: #filePath).deletingLastPathComponent()
            .appending(path: "../../fixtures/cta/\(name)").standardized)
    }

    // MARK: Contents

    /// The same Belmont response and keys as firmware test_train_cards, so relayed records land on the wearable's
    /// own (station, line, platform label) cards.
    @Test func stationRecordsMatchTheWearablesCards() throws {
        let belmont = try #require(index.stop(id: "41320"))
        let preds = try TrainParse.arrivals(fixture("train_arrivals_belmont_live.json"), fetched: fetched)
        let records = WearableRelay.records(for: belmont, predictions: preds, fetched: fetched) { _ in false }
        #expect(records.map(\.route) == ["Brn", "Red", "P", "P", "Red", "Brn"])
        #expect(records.map(\.dir) == ["Loop", "Howard", "Loop", "Linden", "95th/Dan Ryan", "Kimball"])
        #expect(records.map { $0.arrivals.first?.min } == [0, 1, 2, 4, 8, 18])
        #expect(records.allSatisfy { $0.id == "41320" && $0.type == .train && $0.name == "Belmont" })
        #expect(records.allSatisfy { $0.ts == Int(fetched.timeIntervalSince1970) })
        // veh carried for Follow: Brown toward Loop is run 411, Red toward Howard 815 then 915.
        #expect(records[0].arrivals[0].veh == "411")
        #expect(records[1].arrivals.prefix(2).map(\.veh) == ["815", "915"])
    }

    @Test func busRecordsSplitPerRouteAndDirection() throws {
        let stop = try #require(index.stops.first { $0.kind == .bus && $0.lines?.count == 2 })
        let lines = try #require(stop.lines)
        let preds = lines.enumerated().map { i, line in
            Prediction(stopID: stop.id, route: line.route, dir: line.dir, dest: "Somewhere",
                       at: fetched.addingTimeInterval(Double(60 * (5 - i))), live: true, delayed: false, vehicle: "8\(i)")
        } + [Prediction(stopID: "other", route: lines[0].route, dir: lines[0].dir, dest: "x", at: fetched,
                        live: true, delayed: false)]
        let records = WearableRelay.records(for: stop, predictions: preds, fetched: fetched) { $0 == lines[0].route }
        #expect(records.count == lines.count)
        for (i, line) in lines.enumerated() {
            let r = try #require(records.first { $0.route == line.route && $0.dir == line.dir })
            #expect(r.type == .bus && r.arrivals.count == 1 && r.arrivals[0].veh == "8\(i)")
            #expect(r.alert == (line.route == lines[0].route))
        }
        #expect(records.first?.arrivals.first?.min == 4)   // soonest card first
    }

    @Test func linesWithNoArrivalsSendNoCard() throws {
        let belmont = try #require(index.stop(id: "41320"))
        #expect(WearableRelay.records(for: belmont, predictions: [], fetched: fetched) { _ in false }.isEmpty)
    }

    @Test func stopsAreTheNearbyListAndThePin() throws {
        let bus = try #require(index.stops.first { $0.kind == .bus })
        let nearby = NearbyMessage(lat: 0, lon: 0, trains: ["41320"], buses: [bus.id], close: ["41320"])
        let platform = try #require(index.stop(id: "40380")?.platforms?.first?.id)   // Clark/Lake
        let pin = ConfigStop(type: .train, route: "Blue", dir: "O'Hare", id: platform, name: "Clark/Lake")
        #expect(WearableRelay.stops(nearby: nearby, pin: pin, index: index).map(\.id) == ["41320", bus.id, "40380"])
        // The pin already in the list isn't sent twice; unknown ids are skipped.
        let again = ConfigStop(type: .bus, route: "x", dir: "y", id: bus.id, name: "z")
        let odd = NearbyMessage(lat: 0, lon: 0, trains: ["nope"], buses: [bus.id], close: [])
        #expect(WearableRelay.stops(nearby: odd, pin: again, index: index).map(\.id) == [bus.id])
        #expect(WearableRelay.stops(nearby: nil, pin: nil, index: index).isEmpty)
    }

    // MARK: Timing

    @Test func aStopIsSentWhenFetchedSinceItsLastSendOrEvery60s() {
        let t = fetched
        #expect(!WearableRelay.due(fetched: nil, last: nil, now: t))   // nothing to send yet
        #expect(WearableRelay.due(fetched: t, last: nil, now: t))   // first time
        let last = (at: t, fetched: t)
        #expect(!WearableRelay.due(fetched: t, last: last, now: t.addingTimeInterval(30)))   // same data, 30 s on
        #expect(WearableRelay.due(fetched: t.addingTimeInterval(25), last: last, now: t.addingTimeInterval(30)))   // new data
        #expect(WearableRelay.due(fetched: t, last: last, now: t.addingTimeInterval(60)))   // at least every 60 s
    }

    @Test func aFetchIsPushedToEachWearableShowingThatStop() throws {
        let belmont = try #require(index.stop(id: "41320")), bus = try #require(index.stops.first { $0.kind == .bus })
        let sending = [("c3d4", [belmont, bus]), ("e5f6", [bus])]
        let pushed = WearableRelay.pushes([bus.id, "nope"], to: sending)
        #expect(pushed.map { "\($0.0)|\($0.1.id)" } == ["c3d4|\(bus.id)", "e5f6|\(bus.id)"])
        #expect(WearableRelay.pushes([belmont.id], to: []).isEmpty)   // no wearable ticked yet: nothing pushed
    }

    @Test func aReconnectedWearableForgetsItsSends() {
        let sent: WearableRelay.Sent = ["c3d4|41320": (fetched, fetched), "c3d4|1234": (fetched, fetched),
                                        "e5f6|41320": (fetched, fetched)]
        #expect(WearableRelay.forgetting(["c3d4"], in: sent).keys.sorted() == ["e5f6|41320"])
        #expect(WearableRelay.forgetting([], in: sent).count == 3)
    }

    @Test func tickPeriodIsTheSpecs() {
        #expect(Config.relayTickSec == 30 && WearableRelay.resendSec == 60)
    }

    @Test func tickCarriesThePhonesTime() throws {
        let json = try #require(BLEMessage.encode(WearableRelay.tick(now: fetched), limit: BLEFrames.maxMessage))
        #expect(String(decoding: json, as: UTF8.self) == "{\"now\":1762700000}")   // no weather fetch: left out
    }

    @Test func noRelayUntilTheWearableHasItsStops() {
        let n = NearbyMessage(lat: 0, lon: 0, trains: ["41320"], buses: [], close: [])
        #expect(WearableRelay.targets(["c3d4", "e5f6"], sentNearby: ["e5f6": n]) == ["e5f6"])
        #expect(WearableRelay.targets(["c3d4"], sentNearby: [:]).isEmpty)
    }

    // MARK: Foreground only

    @Test func runsInTheForegroundAndStopsInTheBackground() async {
        let relay = WearableRelay(link: DeviceLink(), arrivals: ArrivalsStore(), index: index)
        relay.setActive(true)
        #expect(relay.running)
        NotificationCenter.default.post(name: UIApplication.willResignActiveNotification, object: nil)
        #expect(!relay.running)
        NotificationCenter.default.post(name: UIApplication.didBecomeActiveNotification, object: nil)
        #expect(relay.running)
        relay.setActive(false)
        #expect(!relay.running)
    }
}
