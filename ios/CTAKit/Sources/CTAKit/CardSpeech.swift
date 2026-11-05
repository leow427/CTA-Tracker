import Foundation

/// What VoiceOver reads for a card, as one sentence (app spec 4.6):
/// "Red Line to Howard from Belmont, 3 minutes, live, delayed".
public enum CardSpeech {
    /// "Red Line" for trains, "Route 22" for buses.
    public static func lineName(_ line: Line, kind: Kind) -> String {
        kind == .station ? LineColors.train(line.route)?.name ?? line.route : "Route \(line.route)"
    }

    public static func sentence(line: Line, kind: Kind, stopName: String, next: Prediction?,
                                later: [Prediction] = [], alert: Bool, freshness: Freshness = .fresh,
                                now: Date, night: Bool = false) -> String {
        var parts = ["\(lineName(line, kind: kind)) to \(next?.dest ?? line.dir) from \(stopName)"]
        if let p = next {
            parts += [spoken(p, now: now), p.live ? "live" : "scheduled"]
            if p.delayed { parts.append("delayed") }
        } else {
            parts.append(night ? "no service now" : "no arrivals")
        }
        if !later.isEmpty { parts.append("also " + later.map { spoken($0, now: now) }.joined(separator: " and ")) }
        if alert { parts.append("service alert") }
        switch freshness {   // a dimmed card speaks its age too, so VoiceOver never hears stale data as fresh
        case .fresh: break
        case .dim(let m), .aged(let m): parts.append("updated \(minutes(m)) ago")
        }
        return parts.joined(separator: ", ")
    }

    /// "3 minutes", "1 minute", "due", or "at 10:05" for a scheduled arrival.
    static func spoken(_ p: Prediction, now: Date) -> String {
        guard p.live else { return "at \(ArrivalFormat.clock(p.at))" }
        let m = ArrivalFormat.minutes(until: p.at, now: now)
        return m < 1 ? "due" : minutes(m)
    }

    static func minutes(_ m: Int) -> String { m == 1 ? "1 minute" : "\(m) minutes" }
}

extension ArrivalFormat {
    /// The card's later departures: "also 12, 20 min" when all are live, otherwise each label ("also 12 min, 10:05").
    public static func also(_ later: [Prediction], now: Date) -> String? {
        guard !later.isEmpty else { return nil }
        if later.allSatisfy({ $0.live && minutes(until: $0.at, now: now) >= 1 }) {
            return "also " + later.map { "\(minutes(until: $0.at, now: now))" }.joined(separator: ", ") + " min"
        }
        return "also " + later.map { label($0, now: now) }.joined(separator: ", ")
    }
}
