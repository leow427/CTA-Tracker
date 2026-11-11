import CoreLocation
import Foundation

/// One line and direction at a stop, in stop detail (app spec 5.1).
public struct DetailGroup: Hashable, Identifiable, Sendable {
    public let line: Line
    public let stopID: String          // bus stop or platform
    public let kind: Kind
    public let arrivals: [Prediction]  // up to 4, soonest first
    public var id: String { "\(stopID)|\(line.route)|\(line.dir)" }
}

/// Stop detail's groups: everything at the stop, by line and direction, the tapped card's group first.
/// Trains: the whole station, every line, both directions. Buses: every route at that direction-specific stop.
public enum DetailGroups {
    public static let perGroup = 4

    public static func groups(stop: Stop, tapped: Line?, predictions: [Prediction], now: Date) -> [DetailGroup] {
        let slots = CardBuilder.slots(of: stop, meters: 0)
        let groups = slots.map { slot in
            DetailGroup(line: slot.line, stopID: slot.stopID, kind: slot.kind,
                        arrivals: Array(slot.predictions(from: predictions, now: now).prefix(perGroup)))
        }
        // Tapped first, then a stable line order (CTA's, or route number for buses), then direction A–Z
        // (app spec 5.1: "grouped by line or route, then direction"), so groups never jump as trains leave.
        func rank(_ g: DetailGroup) -> Int { LineColors.trains.firstIndex { $0.route == g.line.route } ?? Int.max }
        return groups.sorted { x, y in
            if (x.line == tapped) != (y.line == tapped) { return x.line == tapped }
            if rank(x) != rank(y) { return rank(x) < rank(y) }
            if x.line.route != y.line.route { return x.line.route.localizedStandardCompare(y.line.route) == .orderedAscending }
            return x.line.dir < y.line.dir
        }
    }

    /// The first moment after `now` when stop detail's page (all but its per-second freshness line) can read
    /// differently: a shown arrival's minutes, a drop-off, the data turning stale, or night hours
    /// (`ArrivalFormat.nextChange`). The page shows no data age in minutes, so those don't count.
    public static func nextChange(after now: Date, stop: Stop, predictions: [Prediction], fetched: Date?) -> Date {
        let cards = CardBuilder.slots(of: stop, meters: 0).map { slot in predictions.filter(slot.matches) }
        return ArrivalFormat.nextChange(after: now, cards: cards, shownPerCard: perGroup, fetched: fetched.map { [$0] } ?? [],
                                        ageMinutes: false)
    }
}

/// Search (app spec 3): favorites first, then stops and routes matching the name or route number:
/// stations, then bus stops, each nearest first.
public enum StopSearch {
    public static let limit = 30

    public struct Results: Sendable {
        public let favorites: [Slot]
        public let stops: [(stop: Stop, meters: Double)]
    }

    /// Names and line keys folded once, so a keystroke is a cheap scan of ~11k stops; distances are computed only
    /// for matches.
    public struct Index: Sendable {
        let stops: StopIndex
        let entries: [Entry]

        struct Entry: Sendable {
            let stop: Stop
            let name: String
            let routes: [String]       // every line's route ID, matched exactly ("22", "brn")
            let lineNames: [String]    // trains' line names, matched by prefix ("red", "brown line")
        }

        public init(_ stops: StopIndex) {
            self.stops = stops
            entries = stops.stops.map { stop in
                let lines = (stop.lines ?? []) + (stop.platforms ?? []).flatMap(\.lines)
                let routes = Array(Set(lines.map(\.route)))
                return Entry(stop: stop, name: StopSearch.normalized(stop.name), routes: routes.map(StopSearch.normalized),
                             lineNames: stop.kind == .bus ? [] : routes.map { StopSearch.normalized(LineColors.train($0)?.name ?? $0) })
            }
        }
    }

    /// `kinds` is the All · Trains · Buses switch, which filters search like the list.
    public static func results(_ query: String, center: CLLocationCoordinate2D, favorites: [Slot],
                               kinds: Set<Kind> = [.bus, .station], index: Index) -> Results {
        let q = normalized(query)
        let favorites = favorites.filter { kinds.contains($0.kind) }
        guard !q.isEmpty else { return Results(favorites: favorites, stops: []) }
        let favs = favorites.filter { slot in
            matchesLine(slot.line, kind: slot.kind, q)
                || (index.stops.stop(id: slot.fetchID).map { normalized($0.name).contains(q) } ?? false)
        }
        let hits = index.entries.filter { e in
            kinds.contains(e.stop.kind) && (e.name.contains(q) || e.routes.contains(q) || e.lineNames.contains { $0.hasPrefix(q) })
        }.map { e in
            (stop: e.stop, meters: StopIndex.meters(center, CLLocationCoordinate2D(latitude: e.stop.lat, longitude: e.stop.lon)))
        }
        // Stations first (a name search usually means the station, not the bus stops named after it), each nearest first.
        let sorted = hits.sorted { a, b in
            if a.stop.kind != b.stop.kind { return a.stop.kind == .station }
            return a.meters != b.meters ? a.meters < b.meters : a.stop.id < b.stop.id
        }
        return Results(favorites: favs, stops: Array(sorted.prefix(limit)))
    }

    static func normalized(_ s: String) -> String {
        s.folding(options: [.caseInsensitive, .diacriticInsensitive], locale: nil).trimmingCharacters(in: .whitespaces)
    }

    /// A route number matches exactly ("22" isn't "122"); a train line by its ID or the start of its name ("red", "brn").
    static func matchesLine(_ line: Line, kind: Kind, _ q: String) -> Bool {
        if kind == .bus { return normalized(line.route) == q }
        let name = normalized(LineColors.train(line.route)?.name ?? line.route)
        return normalized(line.route) == q || name.hasPrefix(q)
    }
}

/// A bus stop usually serves one direction of a route; the other direction stops across the street. This finds that
/// stop, so a bus stop page can switch direction the way a station's page switches platforms (Leo 2026-11-10).
public enum OppositeStop {
    /// How far to look (Leo 2026-11-10): across the street and a little along it, about 5 minutes' walk. App-only, so
    /// not in `Config` (the firmware's mirror).
    public static let maxMeters: Double = 400

    /// The compass opposite of a bus direction, nil for anything else.
    public static func direction(opposite dir: String) -> String? {
        ["Northbound": "Southbound", "Southbound": "Northbound", "Eastbound": "Westbound", "Westbound": "Eastbound"][dir]
    }

    /// The nearest bus stop within `within` meters serving `line`'s route the other way, with that line: `stop`
    /// itself (0 m) when it serves both ways, as a terminal often does; else ties go to the lower ID. Nil when there's
    /// none, or the direction has no opposite.
    public static func find(for stop: Stop, line: Line, in index: StopIndex,
                            within: Double = maxMeters) -> (stop: Stop, line: Line)? {
        guard stop.kind == .bus, let other = direction(opposite: line.dir) else { return nil }
        let want = Line(route: line.route, dir: other)
        if (stop.lines ?? []).contains(want) { return (stop, want) }
        let here = CLLocationCoordinate2D(latitude: stop.lat, longitude: stop.lon)
        var best: (stop: Stop, meters: Double)?
        for other in index.stops where other.kind == .bus && other.id != stop.id && (other.lines ?? []).contains(want) {
            let meters = StopIndex.meters(here, CLLocationCoordinate2D(latitude: other.lat, longitude: other.lon))
            guard meters <= within else { continue }
            if let b = best, b.meters < meters || (b.meters == meters && b.stop.id < other.id) { continue }
            best = (other, meters)
        }
        return best.map { ($0.stop, want) }
    }
}
