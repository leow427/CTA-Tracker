import Foundation

public enum AlertsWeather {
    /// Customer Alerts' HTML as plain text for stop detail: paragraphs and breaks become new lines, tags go,
    /// and the common entities decode.
    static let namedEntities: [String: String] = [
        "nbsp": " ", "amp": "&", "lt": "<", "gt": ">", "quot": "\"", "apos": "'",
        "lsquo": "\u{2018}", "rsquo": "\u{2019}", "ldquo": "\u{201C}", "rdquo": "\u{201D}",
        "mdash": "\u{2014}", "ndash": "\u{2013}", "hellip": "\u{2026}", "bull": "\u{2022}",
    ]

    /// Named entities from the table, and every numeric one (&#8217; and &#x2019;).
    static func decodeEntities(_ text: String) -> String {
        guard let re = try? NSRegularExpression(pattern: "&(#x[0-9a-fA-F]+|#[0-9]+|[a-zA-Z]+);") else { return text }
        var out = "", last = text.startIndex
        for m in re.matches(in: text, range: NSRange(text.startIndex..., in: text)) {
            guard let whole = Range(m.range, in: text), let name = Range(m.range(at: 1), in: text) else { continue }
            out += text[last..<whole.lowerBound]
            let key = String(text[name])
            let scalar: Unicode.Scalar? = key.hasPrefix("#x") ? UInt32(key.dropFirst(2), radix: 16).flatMap(Unicode.Scalar.init)
                : key.hasPrefix("#") ? UInt32(key.dropFirst()).flatMap(Unicode.Scalar.init) : nil
            out += scalar.map { String(Character($0)) } ?? namedEntities[key] ?? String(text[whole])
            last = whole.upperBound
        }
        return out + text[last...]
    }

    public static func plainText(_ html: String) -> String {
        var s = html.replacingOccurrences(of: "<br\\s*/?>|</p>|</li>|</div>", with: "\n", options: [.regularExpression, .caseInsensitive])
        s = s.replacingOccurrences(of: "<[^>]+>", with: "", options: .regularExpression)
        s = decodeEntities(s)
        return s.split(separator: "\n").map { $0.split(whereSeparator: \.isWhitespace).joined(separator: " ") }
            .filter { !$0.isEmpty }.joined(separator: "\n")
    }

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
