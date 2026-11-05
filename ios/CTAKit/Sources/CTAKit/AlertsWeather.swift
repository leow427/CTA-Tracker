import Foundation

public enum AlertsWeather {
    /// Customer Alerts (`outputType=JSON`). `Alert` and `Service` may each be one object instead of an array.
    public static func alerts(_ data: Data) throws -> [ServiceAlert] {
        try JSONDecoder().decode(Response.self, from: data).body.alerts?.items.map { a in
            ServiceAlert(id: a.AlertId, headline: a.Headline, shortText: a.ShortDescription ?? "",
                         fullText: a.FullDescription?.text ?? "",
                          routes: (a.ImpactedService?.Service?.items ?? [])
                              .filter { $0.ServiceType == "R" || $0.ServiceType == "B" }.map(\.ServiceId))
        } ?? []
    }

    /// Distinct alerted routes, in order (mirrors the firmware's parseAlertRoutes).
    public static func routes(_ alerts: [ServiceAlert]) -> [String] {
        var seen: [String] = []
        for r in alerts.flatMap(\.routes) where !seen.contains(r) { seen.append(r) }
        return seen
    }

    /// Open-Meteo `current=temperature_2m,weather_code`.
    public static func weather(_ data: Data) throws -> (code: Int, tempC: Double) {
        let c = try JSONDecoder().decode(Weather.self, from: data).current
        return (c.weather_code, c.temperature_2m)
    }

    /// Base spec 6.3: rain codes give the umbrella (even below 0 °C), then snow codes or frost give the scarf.
    public static func accessory(code: Int, tempC: Double) -> Accessory {
        if (51...67).contains(code) || (80...82).contains(code) || (95...99).contains(code) { return .umbrella }
        if (71...77).contains(code) || (85...86).contains(code) || tempC < 0 { return .scarf }
        return .none
    }

    private struct Response: Decodable {
        let body: Body
        enum CodingKeys: String, CodingKey { case body = "CTAAlerts" }
        struct Body: Decodable {
            let alerts: OneOrMany<A>?
            enum CodingKeys: String, CodingKey { case alerts = "Alert" }
        }
        struct A: Decodable {
            let AlertId: String, Headline: String, ShortDescription: String?
            let FullDescription: CData?, ImpactedService: Impacted?
        }
        struct Impacted: Decodable { let Service: OneOrMany<Service>? }
        struct Service: Decodable { let ServiceType: String, ServiceId: String }
        /// `{"#cdata-section": "…"}`, or occasionally a plain string.
        struct CData: Decodable {
            let text: String
            enum CodingKeys: String, CodingKey { case text = "#cdata-section" }
            init(from decoder: any Decoder) throws {
                if let s = try? decoder.singleValueContainer().decode(String.self) { text = s; return }
                text = try decoder.container(keyedBy: CodingKeys.self).decode(String.self, forKey: .text)
            }
        }
    }

    private struct Weather: Decodable {
        let current: Current
        struct Current: Decodable { let temperature_2m: Double, weather_code: Int }
    }
}
