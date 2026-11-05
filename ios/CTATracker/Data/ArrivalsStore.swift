import CTAKit
import Foundation
import Observation

/// Arrivals for the cards on screen (app spec 4.4): polls every 30 s while the list is visible and the app is in
/// the foreground, keeps the last good results with their age, and refreshes alerts for the listed routes every 5 min.
@Observable final class ArrivalsStore {
    private(set) var book = ArrivalBook()                        // predictions and fetch times by fetchID
    var predictions: [String: [Prediction]] { book.predictions }
    var fetched: [String: Date] { book.fetched }
    private(set) var alerts: [String: [ServiceAlert]] = [:]      // keyed by route
    /// Trains and buses fail separately, so a train outage doesn't blank bus cards (app spec 8).
    private(set) var trainsFailing = false
    private(set) var busesFailing = false
    var unavailable: Bool { trainsFailing || busesFailing }

    private var slots: [Slot] = []
    private var poll: Task<Void, Never>?
    private var pending: Set<String> = []          // added stops waiting for their first fetch
    private var pendingFetch: Task<Void, Never>?
    private var alertsFetched: Date?
    private let keys = Secrets()

    /// Fetch exactly these cards' stops. Newly added ones are fetched on their own, coalesced over 0.4 s, so a
    /// list that changes several times in a row (the switch, a pan) makes one small request, not a full refresh each.
    func track(_ slots: [Slot]) {
        let newRoutes = Set(slots.map(\.line.route)).subtracting(self.slots.map(\.line.route)).subtracting(alerts.keys)
        // A stop fetched within the last poll interval (back from detail, say) doesn't need fetching again.
        let added = Set(slots.map(\.fetchID)).subtracting(self.slots.map(\.fetchID))
            .filter { fetched[$0].map { Date().timeIntervalSince($0) >= Config.listPollSec } ?? true }
        self.slots = slots
        if !newRoutes.isEmpty, poll != nil { Task { await refreshAlerts(routes: newRoutes) } }   // ⚠ for new routes now
        guard !added.isEmpty, poll != nil else { return }
        pending.formUnion(added)
        pendingFetch?.cancel()
        pendingFetch = Task {
            try? await Task.sleep(for: .milliseconds(400))
            guard !Task.isCancelled else { return }
            let ids = pending
            pending = []
            pendingFetch = nil   // once it starts, a later track() starts its own wait and can't cancel this fetch
            await refresh(only: ids)
        }
    }

    /// Polls while the list is on screen and the app is active; `false` stops it (background, stop detail).
    func setActive(_ active: Bool) {
        poll?.cancel()
        poll = nil
        guard active else { return }
        poll = Task {
            while !Task.isCancelled {
                await refresh()
                try? await Task.sleep(for: .seconds(Config.listPollSec))
            }
        }
    }

    func cardPredictions(_ slot: Slot, now: Date) -> [Prediction] {
        slot.predictions(from: predictions[slot.fetchID] ?? [], now: now)
    }

    /// Never loaded and its last request failed: the card shows "—".
    func unknown(_ slot: Slot) -> Bool { book.unknown(slot.fetchID) }

    func freshness(_ slot: Slot, now: Date) -> Freshness {
        fetched[slot.fetchID].map { ArrivalFormat.freshness(fetched: $0, now: now) } ?? .fresh
    }

    func hasAlert(_ slot: Slot) -> Bool { !(alerts[slot.line.route] ?? []).isEmpty }

    /// Refreshes every tracked stop (and alerts when due), or just `only`.
    func refresh(only ids: Set<String>? = nil) async {
        let slots = ids.map { ids in self.slots.filter { ids.contains($0.fetchID) } } ?? self.slots
        #if DEBUG
        if ProcessInfo.processInfo.arguments.contains("-demo") { return demo(slots) }
        #endif
        let stations = Set(slots.filter { $0.kind == .station }.map(\.fetchID))
        let busStops = Array(Set(slots.filter { $0.kind == .bus }.map(\.fetchID))).sorted()

        async let trains = fetchTrains(stations)
        async let buses = fetchBuses(busStops)
        let (t, b) = await (trains, buses)
        // Leaving the list cancels the poll mid-fetch; that's not an outage, so record nothing.
        guard !Task.isCancelled else { return }
        apply(t, asked: stations, failing: &trainsFailing, subset: ids != nil)
        apply(b, asked: Set(busStops), failing: &busesFailing, subset: ids != nil)

        if ids == nil, alertsFetched.map({ Date().timeIntervalSince($0) >= Config.alertsPollSec }) ?? true {
            await refreshAlerts(routes: Set(slots.map(\.line.route)))
        }
    }

    // MARK: - Fetching

    /// fetchID → its predictions, for each request that succeeded; nil when there's no key.
    private typealias Results = [String: (preds: [Prediction], at: Date)]

    private func fetchTrains(_ stations: Set<String>) async -> Results? {
        guard !stations.isEmpty else { return [:] }
        guard let key = keys.train else { return nil }
        return await withTaskGroup(of: (String, [Prediction]?, Date).self) { group in
            for id in stations {
                group.addTask {
                    guard let url = CTAClient.trainArrivalsURL(key: key, id: id) else { return (id, nil, Date()) }
                    let data = try? await CTAClient.get(url)
                    let at = Date()
                    return (id, data.flatMap { try? TrainParse.arrivals($0, fetched: at) }, at)
                }
            }
            var out: Results = [:]
            for await (id, preds, at) in group { if let preds { out[id] = (preds, at) } }
            return out
        }
    }

    private func fetchBuses(_ stopIDs: [String]) async -> Results? {
        guard !stopIDs.isEmpty else { return [:] }
        guard let key = keys.bus else { return nil }
        let chunks = stride(from: 0, to: stopIDs.count, by: Config.busStopsPerCall).map {
            Array(stopIDs[$0..<min($0 + Config.busStopsPerCall, stopIDs.count)])
        }
        return await withTaskGroup(of: ([String], [Prediction]?, Date).self) { group in
            for chunk in chunks {
                group.addTask {
                    guard let url = CTAClient.busPredictionsURL(key: key, stopIDs: chunk) else { return (chunk, nil, Date()) }
                    let data = try? await CTAClient.get(url)
                    let at = Date()
                    return (chunk, data.flatMap { try? BusParse.predictions($0, fetched: at) }, at)
                }
            }
            var out: Results = [:]
            for await (chunk, preds, at) in group {
                guard let preds else { continue }
                for (id, own) in ArrivalBook.split(preds, stops: chunk) { out[id] = (own, at) }
            }
            return out
        }
    }

    /// Records what loaded and marks the rest failed (their last good results stay). The kind counts as
    /// unavailable when nothing it asked for loaded. A fetch of just-added stops that asked this kind nothing
    /// says nothing about it, so its flag stays (a bus-only pan mustn't hide a train outage).
    private func apply(_ results: Results?, asked: Set<String>, failing: inout Bool, subset: Bool) {
        let got = results ?? [:]
        for (id, r) in got { book.record([id: r.preds], at: r.at) }
        book.fail(asked.subtracting(got.keys))
        if subset && asked.isEmpty { return }
        failing = !asked.isEmpty && got.isEmpty
    }

    private func refreshAlerts(routes: Set<String>) async {
        guard let url = CTAClient.alertsURL(routes: routes.sorted()),
              let data = try? await CTAClient.get(url),
              let items = try? AlertsWeather.alerts(data) else { return }
        if routes == Set(slots.map(\.line.route)) { alertsFetched = Date() }
        // Merged per route: a fetch for some routes never erases the others' ⚠ (and a route with none clears).
        let byRoute = Dictionary(grouping: items.flatMap { a in a.routes.map { ($0, a) } }, by: \.0).mapValues { $0.map(\.1) }
        for route in routes { alerts[route] = byRoute[route] ?? [] }
    }

    #if DEBUG
    /// `-demo` launch argument: plausible arrivals for the tracked cards, for simulator screenshots without CTA keys.
    private func demo(_ slots: [Slot]) {
        let now = Date()
        for slot in slots {
            let seed = (slot.stopID + slot.line.route + slot.line.dir).unicodeScalars.reduce(7) { ($0 * 31 + Int($1.value)) % 9973 }
            let minutes = [Double(seed % 12), Double(seed % 12 + 6 + seed % 5), Double(seed % 12 + 14 + seed % 7)]
            let preds = minutes.map {
                Prediction(stopID: slot.stopID, route: slot.line.route, dir: slot.kind == .bus ? slot.line.dir : "",
                           dest: slot.line.dir.hasSuffix("bound") ? "Downtown" : slot.line.dir,
                           at: now.addingTimeInterval($0 * 60 + 20), live: seed % 9 != 0, delayed: seed % 7 == 0)
            }
            let others = (predictions[slot.fetchID] ?? []).filter { !slot.matches($0) }   // other cards at this stop
            book.record([slot.fetchID: others + preds], at: now)
        }
    }
    #endif
}

/// CTA keys from the app's `Secrets.plist` (gitignored; copy `ios/Secrets.example.plist`). Never logged.
struct Secrets {
    let train: String?
    let bus: String?

    init(bundle: Bundle = .main) {
        let dict = bundle.url(forResource: "Secrets", withExtension: "plist")
            .flatMap { NSDictionary(contentsOf: $0) as? [String: String] } ?? [:]
        func key(_ name: String) -> String? {
            dict[name].flatMap { $0.isEmpty || $0.hasPrefix("YOUR_") ? nil : $0 }
        }
        train = key("CTA_TRAIN_KEY")
        bus = key("CTA_BUS_KEY")
    }
}
