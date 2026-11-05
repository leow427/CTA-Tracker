import Foundation

/// URL builders for every service the app calls (mirrors the firmware's urls.cpp).
/// The URLs carry the API key, so they're never logged.
public enum CTAClient {
    static let train = "https://lapi.transitchicago.com/api/1.0/"
    static let bus = "https://www.ctabustracker.com/bustime/api/v2/"

    /// A parent station ID (4xxxx) uses `mapid`; a platform ID (3xxxx) uses `stpid`.
    public static func trainArrivalsURL(key: String, id: String) -> URL? {
        url(train + "ttarrivals.aspx", [("key", key), (id.hasPrefix("3") ? "stpid" : "mapid", id), ("outputType", "JSON")])
    }

    public static func trainPositionsURL(key: String, route: String) -> URL? {
        url(train + "ttpositions.aspx", [("key", key), ("rt", route), ("outputType", "JSON")])
    }

    /// 1–10 stops per call.
    public static func busPredictionsURL(key: String, stopIDs: [String]) -> URL? {
        guard (1...Config.busStopsPerCall).contains(stopIDs.count) else { return nil }
        return url(bus + "getpredictions", [("key", key), ("stpid", stopIDs.joined(separator: ",")), ("format", "json")])
    }

    /// 1–10 routes per call.
    public static func busVehiclesURL(key: String, routes: [String]) -> URL? {
        guard (1...10).contains(routes.count) else { return nil }
        return url(bus + "getvehicles", [("key", key), ("rt", routes.joined(separator: ",")), ("format", "json")])
    }

    public static func busPatternsURL(key: String, route: String) -> URL? {
        url(bus + "getpatterns", [("key", key), ("rt", route), ("format", "json")])
    }

    public static func alertsURL(routes: [String]) -> URL? {
        guard !routes.isEmpty else { return nil }
        return url("https://www.transitchicago.com/api/1.0/alerts.aspx",
                   [("outputType", "JSON"), ("activeonly", "true"), ("routeid", routes.joined(separator: ","))])
    }

    /// 4 decimals (about 11 m) is plenty for weather.
    public static func weatherURL(lat: Double, lon: Double) -> URL? {
        URL(string: String(format: "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,weather_code", lat, lon))
    }

    /// GET with a 15 s timeout; throws `CTAError.http` on anything but 200. Never logs the URL (it holds the key).
    public static func get(_ url: URL, session: URLSession = .shared) async throws -> Data {
        var request = URLRequest(url: url, timeoutInterval: 15)
        request.cachePolicy = .reloadIgnoringLocalCacheData
        let (data, response) = try await session.data(for: request)
        let status = (response as? HTTPURLResponse)?.statusCode ?? -1
        guard status == 200 else { throw CTAError.http(status: status) }
        return data
    }

    private static func url(_ base: String, _ query: [(String, String)]) -> URL? {
        var c = URLComponents(string: base)
        c?.queryItems = query.map { URLQueryItem(name: $0.0, value: $0.1) }
        return c?.url
    }
}
