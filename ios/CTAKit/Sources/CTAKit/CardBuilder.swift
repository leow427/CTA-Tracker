import CoreLocation
import Foundation

/// One card: a line at one stop.
public struct Slot: Hashable, Sendable {
    public let line: Line
    public let stopID: String    // bus stop ID or platform ID
    public let fetchID: String   // bus stop ID, or the parent station ID for trains
    public let kind: Kind
    public let meters: Double    // from the list's center
}

extension Slot: Identifiable {
    /// A card's identity: its stop and line, not its distance, so cards keep their state while the user moves.
    public var id: String { "\(stopID)|\(line.route)|\(line.dir)" }

    /// Whether an arrival belongs to this card: its platform (or bus stop) and route, and for buses its direction
    /// too (a train platform implies the direction; Train Tracker sends no rtdir).
    public func matches(_ p: Prediction) -> Bool {
        p.stopID == stopID && p.route == line.route && (kind == .station || p.dir == line.dir)
    }

    /// This card's arrivals, soonest first, without those more than a minute gone: a stale card must not show
    /// trains that already left as "Due" (review focus 4).
    public func predictions(from all: [Prediction], now: Date) -> [Prediction] {
        all.filter { matches($0) && $0.at.timeIntervalSince(now) > -Config.departedGraceSec }.sorted { $0.at < $1.at }
    }
}

/// The list's order (app spec 4.2): by distance to the card's stop, then by soonest arrival; cards with
/// no arrivals go last at their stop.
public enum CardOrder {
    public static func sorted(_ slots: [Slot], predictions: [Prediction], now: Date) -> [Slot] {
        let soonest = Dictionary(slots.map { ($0, $0.predictions(from: predictions, now: now).first?.at ?? .distantFuture) },
                                 uniquingKeysWith: { a, _ in a })
        return slots.enumerated().sorted { a, b in
            if a.element.meters != b.element.meters { return a.element.meters < b.element.meters }
            if soonest[a.element]! != soonest[b.element]! { return soonest[a.element]! < soonest[b.element]! }
            return a.offset < b.offset
        }.map(\.element)
    }

    /// The first moment after `now` when `sorted` can order these cards differently, nil if never (until new data):
    /// cards differ first by distance, so only cards at the same distance as another can swap, and only when one's
    /// soonest arrival drops off. A hair past it, since the drop-off rule compares strictly.
    public static func nextChange(after now: Date, _ slots: [Slot], predictions: [Prediction]) -> Date? {
        let shared = Dictionary(grouping: slots, by: \.meters).filter { $0.value.count > 1 }.values.joined()
        return shared.compactMap { slot in
            slot.predictions(from: predictions, now: now).first
                .map { $0.at.addingTimeInterval(Config.departedGraceSec + ArrivalFormat.changeLag) }
                .flatMap { $0 > now ? $0 : nil }
        }.min()
    }
}

/// Arrivals by fetch ID, kept across failures (app spec 8: the last cards stay, dimmed, with their age).
public struct ArrivalBook: Sendable {
    public private(set) var predictions: [String: [Prediction]] = [:]
    public private(set) var fetched: [String: Date] = [:]
    private var failed: Set<String> = []

    public init() {}

    /// A Bus Tracker chunk answers for up to 10 stops at once: each asked-about stop gets its own list, [] if none.
    public static func split(_ preds: [Prediction], stops: [String]) -> [String: [Prediction]] {
        Dictionary(uniqueKeysWithValues: stops.map { id in (id, preds.filter { $0.stopID == id }) })
    }

    public mutating func record(_ results: [String: [Prediction]], at: Date) {
        for (id, preds) in results {
            predictions[id] = preds
            fetched[id] = at
            failed.remove(id)
        }
    }

    /// The last request for these failed; their last good results stay.
    public mutating func fail(_ ids: some Sequence<String>) {
        failed.formUnion(ids)
    }

    /// Never loaded, and the last try failed: the card says "—" rather than claiming no arrivals.
    public func unknown(_ id: String) -> Bool { fetched[id] == nil && failed.contains(id) }
}

public struct SlotList: Hashable, Sendable {
    public let favorites: [Slot]
    public let nearby: [Slot]
    public let farFromChicago: Bool

    public init(favorites: [Slot], nearby: [Slot], farFromChicago: Bool) {
        self.favorites = favorites; self.nearby = nearby; self.farFromChicago = farFromChicago
    }
}

/// The list's card rules (app spec 4.2, 8): radius, fallback, one slot per line, favorites dedupe.
public enum CardBuilder {
    public static func slots(center: CLLocationCoordinate2D, filter: KindFilter,
                             favorites: [Favorite], index: StopIndex) -> SlotList {
        let kinds = filter.kinds

        // Favorites: known stops still served by the line, in the switch's kind; the ★ section shows 3.
        let favs = resolved(favorites: favorites, center: center, index: index)
            .filter { kinds.contains($0.kind) }.prefix(Config.maxFavorites)

        // Each distance once, and only the stops in the radius sorted: this runs on the main thread after every pan,
        // and sorting all ~11k stops there was most of its cost. Same order as `StopIndex.byDistance`.
        var nearestAny = Double.infinity
        var candidates: [(stop: Stop, meters: Double)] = []
        for stop in index.stops {
            let meters = distance(center, stop)
            nearestAny = min(nearestAny, meters)
            if kinds.contains(stop.kind) { candidates.append((stop, meters)) }
        }
        // Far from Chicago looks at every kind, and beats the fallback.
        guard nearestAny <= Config.farFromChicagoM else {
            return SlotList(favorites: Array(favs), nearby: [], farFromChicago: true)
        }

        let inRadius = candidates.filter { $0.meters <= Config.cardRadiusM }.sorted(by: closer)
        let shown = inRadius.isEmpty ? nearest(Config.fallbackStops, of: candidates) : inRadius

        // A favorite hides its line only when its own stop is listed (app spec 4.2).
        // Stops come nearest first, so the first slot seen for a line is its nearest.
        let shownIDs = Set(shown.map(\.stop.id))
        var seen = Set(favs.filter { shownIDs.contains($0.fetchID) }.map(\.line))
        let nearby = shown.flatMap { slots(of: $0.stop, meters: $0.meters) }.filter { seen.insert($0.line).inserted }
        return SlotList(favorites: Array(favs), nearby: nearby, farFromChicago: false)
    }

    /// Every saved favorite that still resolves to a card, in saved order: unknown stops, and lines that no longer
    /// serve their stop, are left out (plan 4 decision 3). Not capped: past 3 they show under ★ in search (app spec 2).
    public static func resolved(favorites: [Favorite], center: CLLocationCoordinate2D, index: StopIndex) -> [Slot] {
        favorites.compactMap { fav -> Slot? in
            guard let stop = index.stop(id: fav.stopID) ?? index.station(ofPlatform: fav.stopID) else { return nil }
            return slots(of: stop, meters: distance(center, stop)).first { $0.stopID == fav.stopID && $0.line == fav.line }
        }
    }

    /// Every card a stop can make: each line at a bus stop, or each line at each platform of a station.
    public static func slots(of stop: Stop, meters: Double) -> [Slot] {
        switch stop.kind {
        case .bus:
            (stop.lines ?? []).map { Slot(line: $0, stopID: stop.id, fetchID: stop.id, kind: .bus, meters: meters) }
        case .station:
            (stop.platforms ?? []).flatMap { p in
                p.lines.map { Slot(line: $0, stopID: p.id, fetchID: stop.id, kind: .station, meters: meters) }
            }
        }
    }

    /// `StopIndex.byDistance`'s order: nearer first, then by ID.
    private static func closer(_ a: (stop: Stop, meters: Double), _ b: (stop: Stop, meters: Double)) -> Bool {
        a.meters != b.meters ? a.meters < b.meters : a.stop.id < b.stop.id
    }

    /// The `n` first of `list` in `closer` order, without sorting all of it (ties keep their order, like the sort).
    private static func nearest(_ n: Int, of list: [(stop: Stop, meters: Double)]) -> [(stop: Stop, meters: Double)] {
        var best: [(stop: Stop, meters: Double)] = []
        for e in list where best.count < n || closer(e, best[best.count - 1]) {
            best.insert(e, at: best.firstIndex { closer(e, $0) } ?? best.count)
            if best.count > n { best.removeLast() }
        }
        return best
    }

    static func distance(_ center: CLLocationCoordinate2D, _ stop: Stop) -> Double {
        StopIndex.meters(center, CLLocationCoordinate2D(latitude: stop.lat, longitude: stop.lon))
    }
}
