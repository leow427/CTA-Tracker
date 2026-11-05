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

public struct SlotList: Hashable, Sendable {
    public let favorites: [Slot]
    public let nearby: [Slot]
    public let farFromChicago: Bool
}

/// The list's card rules (app spec 4.2, 8): radius, fallback, one slot per line, favorites dedupe.
public enum CardBuilder {
    public static func slots(center: CLLocationCoordinate2D, filter: KindFilter,
                             favorites: [Favorite], index: StopIndex) -> SlotList {
        let kinds: Set<Kind> = switch filter {
        case .all: [.bus, .station]
        case .trains: [.station]
        case .buses: [.bus]
        }

        // Favorites: known stops still served by the line, in the switch's kind, at most 3.
        let favs = favorites.compactMap { fav -> Slot? in
            let stop = index.stop(id: fav.stopID) ?? index.station(ofPlatform: fav.stopID)
            guard let stop, kinds.contains(stop.kind) else { return nil }
            return slots(of: stop, meters: distance(center, stop)).first { $0.stopID == fav.stopID && $0.line == fav.line }
        }.prefix(Config.maxFavorites)

        // Far from Chicago looks at every kind, and beats the fallback.
        let all = index.byDistance(from: center)
        guard let nearestAny = all.first?.meters, nearestAny <= Config.farFromChicagoM else {
            return SlotList(favorites: Array(favs), nearby: [], farFromChicago: true)
        }

        let sorted = all.filter { kinds.contains($0.stop.kind) }
        let inRadius = sorted.prefix { $0.meters <= Config.cardRadiusM }
        let shown = inRadius.isEmpty ? sorted.prefix(Config.fallbackStops) : inRadius

        // A favorite hides its line only when its own stop is listed (app spec 4.2).
        // Stops come nearest first, so the first slot seen for a line is its nearest.
        let shownIDs = Set(shown.map(\.stop.id))
        var seen = Set(favs.filter { shownIDs.contains($0.fetchID) }.map(\.line))
        let nearby = shown.flatMap { slots(of: $0.stop, meters: $0.meters) }.filter { seen.insert($0.line).inserted }
        return SlotList(favorites: Array(favs), nearby: nearby, farFromChicago: false)
    }

    static func slots(of stop: Stop, meters: Double) -> [Slot] {
        switch stop.kind {
        case .bus:
            (stop.lines ?? []).map { Slot(line: $0, stopID: stop.id, fetchID: stop.id, kind: .bus, meters: meters) }
        case .station:
            (stop.platforms ?? []).flatMap { p in
                p.lines.map { Slot(line: $0, stopID: p.id, fetchID: stop.id, kind: .station, meters: meters) }
            }
        }
    }

    static func distance(_ center: CLLocationCoordinate2D, _ stop: Stop) -> Double {
        StopIndex.meters(center, CLLocationCoordinate2D(latitude: stop.lat, longitude: stop.lon))
    }
}
