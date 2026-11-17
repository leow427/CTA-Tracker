import CoreLocation
import CTAKit
import Foundation
import Observation

/// The open stop's line on the map (F6, app spec 5.2): every train or bus on it, polled every 30 s while stop detail
/// is open and the app is active, where each one is relative to the open stop, and the one the user focused.
@Observable final class LiveVehicles {
    /// The line polled, and the stop whose distance the places count to.
    private(set) var route: String?
    private(set) var kind: Kind = .station
    private(set) var target: String?
    /// The last good fetch; nil before one lands, and once failures have gone on too long to show it.
    private(set) var vehicles: [Vehicle]?
    /// Where each vehicle is, worked out once per fetch (the map and the page both read it).
    private(set) var places: [String: VehiclePlace] = [:]
    private(set) var failing = false
    /// The focused vehicle's ID, and whether a fetch since the focus had it (so a drop-out reads as lost).
    private(set) var focus: String?
    private(set) var seen = false

    @ObservationIgnored private var lastFetch = Date.distantPast
    @ObservationIgnored private var lastGood = Date.distantPast
    /// Bus routes' patterns, which don't change during a session.
    @ObservationIgnored private var patterns: [String: [Pattern]] = [:]
    @ObservationIgnored private let keys = Secrets()

    /// Every L line's platforms in order, for "3 stops away".
    static let runs: [TrainRun] = Bundle.main.url(forResource: "train_lines", withExtension: "json")
        .flatMap { try? Data(contentsOf: $0) }.flatMap { try? TrainRun.decode($0) } ?? []

    /// A failing feed keeps its last positions this long, then shows none rather than old ones.
    static let keepSec: TimeInterval = 90

    var focusStatus: VehicleFocus? {
        focus.map { VehicleFocus.of($0, vehicles: vehicles, seen: seen, failing: failing) }
    }

    /// Tapping the focused vehicle again clears it; any other takes the focus.
    func toggleFocus(_ id: String) {
        focus = focus == id ? nil : id
        seen = vehicles?.contains { $0.id == id } ?? false
    }

    /// Stop detail closed: nothing on the map, no focus.
    func reset() {
        (route, target, vehicles, places, focus, seen, failing) = (nil, nil, nil, [:], nil, false, false)
        lastFetch = .distantPast
    }

    /// Polls the line's positions at most every 30 s until the task is cancelled (the stop closes, the line changes,
    /// or the app leaves the foreground). A new line starts empty and unfocused.
    func poll(route: String, kind: Kind, target: String, index: StopIndex?) async {
        if self.route != route || self.kind != kind {
            reset()
            (self.route, self.kind) = (route, kind)
        }
        #if DEBUG
        if ProcessInfo.processInfo.arguments.contains("-demo") { return }   // demo arrivals have no vehicles
        #endif
        if self.target != target { self.target = target; place(index: index) }
        // Ends when cancelled, or when the stop closed or another line took over (`reset`) before the cancel landed.
        while !Task.isCancelled, self.route == route {
            let wait = lastFetch.addingTimeInterval(Config.livePollSec).timeIntervalSinceNow
            if wait > 0 { try? await Task.sleep(for: .seconds(wait)); continue }
            await refresh(route: route, kind: kind, index: index)
        }
    }

    private func refresh(route: String, kind: Kind, index: StopIndex?) async {
        if kind == .bus, patterns[route] == nil, let key = keys.bus, let url = CTAClient.busPatternsURL(key: key, route: route),
           let data = try? await CTAClient.get(url), let got = try? BusParse.patterns(data) {
            patterns[route] = got
        }
        let got: [Vehicle]?
        if kind == .bus {
            let url = keys.bus.flatMap { CTAClient.busVehiclesURL(key: $0, routes: [route]) }
            got = await Self.fetch(url, BusParse.vehicles)
        } else {
            let url = keys.train.flatMap { CTAClient.trainPositionsURL(key: $0, route: route) }
            got = await Self.fetch(url, TrainParse.positions)
        }
        // Cancelled mid-fetch (the page's task restarts as it opens), it doesn't count: the next poll fetches at once.
        // A stop that closed or changed line meanwhile isn't this fetch's.
        guard !Task.isCancelled, self.route == route else { return }
        lastFetch = .now
        if let got {
            (vehicles, failing, lastGood) = (got, false, .now)
            if let focus, got.contains(where: { $0.id == focus }) { seen = true }
        } else {
            failing = true
            if Date().timeIntervalSince(lastGood) > Self.keepSec { vehicles = nil }
        }
        place(index: index)
    }

    private static func fetch(_ url: URL?, _ parse: (Data) throws -> [Vehicle]) async -> [Vehicle]? {
        guard let url, let data = try? await CTAClient.get(url) else { return nil }
        return try? parse(data)
    }

    private func place(index: StopIndex?) {
        guard let route, let vehicles else { places = [:]; return }
        let coordinate: (String) -> CLLocationCoordinate2D? = { id in
            index?.station(ofPlatform: id).map { CLLocationCoordinate2D(latitude: $0.lat, longitude: $0.lon) }
        }
        let busPatterns = patterns[route] ?? []
        places = Dictionary(vehicles.compactMap { v in
            let place = kind == .bus
                ? VehiclePlace.bus(v, target: target, patterns: busPatterns)
                : VehiclePlace.train(v, route: route, target: target, runs: Self.runs, coordinate: coordinate)
            return place.map { (v.id, $0) }
        }, uniquingKeysWith: { first, _ in first })
    }
}
