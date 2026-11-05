import CoreLocation
import Foundation

/// Every bus stop and station from `data/stops.json`, searched by distance.
/// shortcut: linear scans over ~11k stops, build a spatial index if profiling shows it's slow.
public struct StopIndex: Sendable {
    public static let earthRadius: Double = 6_371_000

    public let stops: [Stop]
    private let byID: [String: Stop]
    private let stationOfPlatform: [String: Stop]

    public init(stops: [Stop]) {
        self.stops = stops
        byID = Dictionary(stops.map { ($0.id, $0) }, uniquingKeysWith: { first, _ in first })
        var platforms: [String: Stop] = [:]
        for s in stops { for p in s.platforms ?? [] { platforms[p.id] = s } }
        stationOfPlatform = platforms
    }

    public init(data: Data) throws {
        self.init(stops: try JSONDecoder().decode([Stop].self, from: data))
    }

    public func stop(id: String) -> Stop? { byID[id] }
    public func station(ofPlatform id: String) -> Stop? { stationOfPlatform[id] }

    /// Stops sorted by distance from `center`, with their distance in meters.
    public func byDistance(from center: CLLocationCoordinate2D, kinds: Set<Kind> = [.bus, .station]) -> [(stop: Stop, meters: Double)] {
        stops.filter { kinds.contains($0.kind) }
            .map { ($0, Self.meters(center, CLLocationCoordinate2D(latitude: $0.lat, longitude: $0.lon))) }
            .sorted { $0.1 != $1.1 ? $0.1 < $1.1 : $0.0.id < $1.0.id }
    }

    /// Great-circle distance by haversine.
    public static func meters(_ a: CLLocationCoordinate2D, _ b: CLLocationCoordinate2D) -> Double {
        let rad = Double.pi / 180
        let dLat = (b.latitude - a.latitude) * rad, dLon = (b.longitude - a.longitude) * rad
        let h = pow(sin(dLat / 2), 2) + cos(a.latitude * rad) * cos(b.latitude * rad) * pow(sin(dLon / 2), 2)
        return 2 * earthRadius * asin(min(1, sqrt(h)))
    }
}
