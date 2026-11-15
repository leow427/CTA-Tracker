import CoreLocation
import Foundation

/// The wearable's `nearby` (ecosystem spec 5.5). Both builders keep to what its parseNearby accepts: no repeats, trains
/// by station ID, at most 5 a list, `close` at most 3 and only IDs from the lists.
public enum NearbyBuilder {
    /// From a location fix, as `tools/send_nearby.py` builds it: the 5 nearest stations; each route and direction's
    /// nearest bus stop, then the 5 nearest of those; `close` the 3 nearest of either.
    public static func fromLocation(_ c: CLLocationCoordinate2D, index: StopIndex) -> NearbyMessage {
        // shortcut: haversine where the script uses a flat-earth km; stops within a meter could swap, harmless.
        let trains = index.byDistance(from: c, kinds: [.station]).prefix(Config.nearbyPerTab)
        var routes = Set<Line>()
        var buses: [(stop: Stop, meters: Double)] = []
        // Nearest first, so a stop that brings any route and direction not seen yet is that one's nearest.
        for s in index.byDistance(from: c, kinds: [.bus]) where buses.count < Config.nearbyPerTab {
            // filter, not contains(where:): every line must be marked seen, not just the first new one.
            if !(s.stop.lines ?? []).filter({ routes.insert($0).inserted }).isEmpty { buses.append(s) }
        }
        let close = (trains + buses).sorted { $0.meters < $1.meters }.prefix(Config.hypedClosestStops)
        return NearbyMessage(lat: c.latitude, lon: c.longitude, trains: trains.map(\.stop.id),
                             buses: buses.map(\.stop.id), close: close.map(\.stop.id))
    }

    /// Mirror: the stops behind the phone's card list, in list order (favorites, then nearby). A station has a card
    /// per line and direction, all fetched by the parent station ID, so each stop is listed once. `close` is the 3
    /// nearest listed stops, as the phone's own Trackie counts them: a far favorite can't make the wrist Hyped.
    public static func fromList(_ slots: SlotList, center: CLLocationCoordinate2D) -> NearbyMessage {
        var trains: [String] = [], buses: [String] = []
        var added: [(id: String, meters: Double)] = []
        for slot in slots.favorites + slots.nearby {
            guard !trains.contains(slot.fetchID), !buses.contains(slot.fetchID) else { continue }
            if slot.kind == .station, trains.count < Config.nearbyPerTab {
                trains.append(slot.fetchID)
            } else if slot.kind == .bus, buses.count < Config.nearbyPerTab {
                buses.append(slot.fetchID)
            } else {
                continue
            }
            added.append((slot.fetchID, slot.meters))
        }
        // Ties keep list order.
        let close = added.enumerated().sorted { ($0.element.meters, $0.offset) < ($1.element.meters, $1.offset) }
            .prefix(Config.hypedClosestStops).map(\.element.id)
        return NearbyMessage(lat: center.latitude, lon: center.longitude, trains: trains, buses: buses, close: close)
    }
}

/// At most one `nearby` per 10 s, and only when the stops changed (Review Focus 5). An offer that comes too soon is
/// held; `flush` sends the latest held one once it's due.
public struct NearbyThrottle: Sendable {
    public static let minInterval: TimeInterval = 10

    private var sentStops: [[String]]?
    private var sentAt = Date.distantPast
    private var held: NearbyMessage?

    public init() {}

    /// The message to send now, or nil (held, or the same stops as the last one sent).
    public mutating func offer(_ m: NearbyMessage, now: Date) -> NearbyMessage? {
        held = nil
        guard Self.stops(m) != sentStops else { return nil }
        // A clock set back is due at once, or every offer would wait until it caught up.
        guard now >= sentAt.addingTimeInterval(Self.minInterval) || now < sentAt else { held = m; return nil }
        sentStops = Self.stops(m)
        sentAt = now
        return m
    }

    /// Records a message sent around the throttle (the wearable asked, or recenter), so the next offer compares with it.
    public mutating func sent(_ m: NearbyMessage, now: Date) {
        held = nil
        sentStops = Self.stops(m)
        sentAt = now
    }

    /// The held message once it's due, else nil.
    public mutating func flush(now: Date) -> NearbyMessage? {
        guard let m = held else { return nil }
        return offer(m, now: now)
    }

    private static func stops(_ m: NearbyMessage) -> [[String]] { [m.trains, m.buses, m.close] }
}
