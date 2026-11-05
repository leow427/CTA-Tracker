import Foundation

public struct Arrival: Codable, Hashable, Sendable {
    public var min: Int
    public var dest: String
    public var live: Bool
    public var delayed: Bool
}

/// The device-shaped stop record (base spec 6.2): relay snapshots and mood rules use it.
public struct StopRecord: Codable, Hashable, Sendable {
    public enum RecordType: String, Codable, Sendable { case train, bus }

    public var id: String
    public var type: RecordType
    public var route: String
    public var dir: String
    public var name: String
    public var arrivals: [Arrival]
    public var alert: Bool
    public var ts: Int   // Unix time of the fetch; 0 = never

    /// Builds a record with the C++ rules: the soonest 4 arrivals, minutes floored and clamped at 0,
    /// route and dir from the soonest arrival, and names cut to 24 bytes.
    /// `fetched` is when the predictions arrived, not now: it becomes `ts` and the minutes count from it,
    /// so a record rebuilt from cached data still reads as stale.
    public static func make(id: String, kind: Kind, name: String, from predictions: [Prediction],
                            alert: Bool, fetched: Date) -> StopRecord {
        let soonest = predictions.sorted { $0.at < $1.at }.prefix(Config.maxArrivals)
        return StopRecord(
            id: id.truncatedUTF8(Config.idLen),
            type: kind == .bus ? .bus : .train,
            route: (soonest.first?.route ?? "").truncatedUTF8(Config.idLen),
            dir: (soonest.first?.dir ?? "").truncatedUTF8(Config.nameLen),
            name: name.truncatedUTF8(Config.nameLen),
            arrivals: soonest.map {
                Arrival(min: ArrivalFormat.minutes(until: $0.at, now: fetched),
                        dest: $0.dest.truncatedUTF8(Config.nameLen), live: $0.live, delayed: $0.delayed)
            },
            alert: alert,
            ts: Int(fetched.timeIntervalSince1970))
    }

    /// The JSON for one BLE write, or nil if it's over 512 bytes.
    public func encoded() -> Data? { BLEMessage.encode(self) }

    /// Parses a record, keeping at most 4 arrivals and cutting names to the device limits. Nil if malformed.
    public static func decode(_ data: Data) -> StopRecord? {
        guard var r = try? JSONDecoder().decode(StopRecord.self, from: data) else { return nil }
        r.id = r.id.truncatedUTF8(Config.idLen)
        r.route = r.route.truncatedUTF8(Config.idLen)
        r.dir = r.dir.truncatedUTF8(Config.nameLen)
        r.name = r.name.truncatedUTF8(Config.nameLen)
        r.arrivals = r.arrivals.prefix(Config.maxArrivals).map {
            var a = $0
            a.dest = a.dest.truncatedUTF8(Config.nameLen)
            return a
        }
        return r
    }
}
