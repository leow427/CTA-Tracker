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

/// Search (app spec 3): favorites first, then stops and routes matching what's typed, the way people say it
/// ("clark belmont", "belmont & clark", "22 clark", "red belmont", "belm"): stations, then bus stops, best match first.
public enum StopSearch {
    public static let limit = 30

    public struct Results: Sendable {
        public let favorites: [Slot]
        public let stops: [(stop: Stop, meters: Double)]
        /// A typed word matched nothing, so it matched close spellings instead ("belmnt" finds Belmont).
        public let closeMatches: Bool
    }

    /// Names and line keys split into words once, so a keystroke is a cheap scan of ~11k stops; distances are
    /// computed only for matches.
    public struct Index: Sendable {
        let stops: StopIndex
        let entries: [Entry]
        let vocabulary: [String]                           // every distinct name word
        let spellings: [(word: String, letters: [UInt8])]  // its letters-only words, for close matches
        let allRoutes: [String]
        let allLineWords: [String]

        struct Entry: Sendable {
            let stop: Stop
            let words: [String]        // the name's words ("clark", "belmont")
            let routes: [String]       // every line's route ID, matched exactly ("22", "brn")
            let lineWords: [String]    // trains' line-name words, matched by prefix ("red", "line")
        }

        public init(_ stops: StopIndex) {
            self.stops = stops
            entries = stops.stops.map { stop in
                let lines = (stop.lines ?? []) + (stop.platforms ?? []).flatMap(\.lines)
                let routes = Array(Set(lines.map(\.route)))
                return Entry(stop: stop, words: StopSearch.words(stop.name), routes: routes.map(StopSearch.normalized),
                             lineWords: stop.kind == .bus ? [] : StopSearch.lineWords(routes))
            }
            vocabulary = Array(Set(entries.flatMap(\.words)))
            spellings = vocabulary.filter { $0.allSatisfy(\.isLetter) }.map { ($0, Array($0.utf8)) }
            allRoutes = Array(Set(entries.flatMap(\.routes)))
            allLineWords = Array(Set(entries.flatMap(\.lineWords)))
        }
    }

    /// `kinds` is the All · Trains · Buses switch, which filters search like the list.
    public static func results(_ query: String, center: CLLocationCoordinate2D, favorites: [Slot],
                               kinds: Set<Kind> = [.bus, .station], index: Index) -> Results {
        let q = queryWords(query)
        let favorites = favorites.filter { kinds.contains($0.kind) }
        guard !q.isEmpty else { return Results(favorites: favorites, stops: [], closeMatches: false) }
        // A word matching nothing at all may be misspelled: it also matches the name words spelled close to it, except a
        // street another typed word already names ("damen dams" means Adams, not Damen again). A word that matches
        // something keeps only its exact matches, so a correct search's results never change, and close matches never
        // share a list with exact ones. When that finds nothing, a typo may have started some other word ("van burn",
        // "burnham"), so every word gets its close spellings and the scan runs again.
        func close(_ fuzz: (Int) -> Bool) -> [Set<String>?] {
            q.indices.map { i in
                guard fuzz(i) else { return nil }
                let near = Set(closeSpellings(of: q[i], in: index.spellings))
                    .subtracting(q.indices.filter { $0 != i }.map { q[$0] })
                return near.isEmpty ? nil : near
            }
        }
        func scan(_ close: [Set<String>?]) -> (favs: [Slot], hits: [(stop: Stop, meters: Double, strong: Bool)]) {
            let closeName = { (i: Int, name: [String]) in close[i].map { name.contains(where: $0.contains) } ?? false }
            let favs = favorites.filter { slot in
                let name = index.stops.stop(id: slot.fetchID).map { words($0.name) } ?? []
                let lines = slot.kind == .bus ? [] : lineWords([slot.line.route])
                return q.indices.allSatisfy {
                    closeName($0, name) || matches(q[$0], name: name, routes: [normalized(slot.line.route)], lineWords: lines)
                }
            }
            var hits: [(stop: Stop, meters: Double, strong: Bool)] = []
            for e in index.entries where kinds.contains(e.stop.kind) {
                let strong = q.indices.allSatisfy { closeName($0, e.words) || inName(q[$0], e.words) || e.routes.contains(q[$0]) }
                guard strong || q.indices.allSatisfy({
                    closeName($0, e.words) || matches(q[$0], name: e.words, routes: e.routes, lineWords: e.lineWords)
                }) else { continue }
                hits.append((e.stop, StopIndex.meters(center, CLLocationCoordinate2D(latitude: e.stop.lat, longitude: e.stop.lon)), strong))
            }
            return (favs, hits)
        }
        var near = close { !matches(q[$0], name: index.vocabulary, routes: index.allRoutes, lineWords: index.allLineWords) }
        var (favs, hits) = scan(near)
        if favs.isEmpty, hits.isEmpty {
            let all = close { _ in true }
            if all != near { (near, (favs, hits)) = (all, scan(all)) }
        }
        // Stations first (a name search usually means the station, not the bus stops named after it); then stops where
        // every word is in the name or is one of the stop's routes, ahead of those where a word only started a train line's
        // name ("red"); nearest first within each.
        let sorted = hits.sorted { a, b in
            if a.stop.kind != b.stop.kind { return a.stop.kind == .station }
            if a.strong != b.strong { return a.strong }
            return a.meters != b.meters ? a.meters < b.meters : a.stop.id < b.stop.id
        }
        return Results(favorites: favs, stops: sorted.prefix(limit).map { ($0.stop, $0.meters) },
                       closeMatches: near.contains { $0 != nil })
    }

    /// Name words spelled close to a typed word: 1 typo (a swapped pair counts as one) in 4–6 letters, 2 in 7 or more,
    /// none under 4 letters or with a digit (routes and numbered streets are typed exactly). The word may be a misspelled
    /// start ("fullertn" on the way to "fullerton"), so it's compared with each name word's closest start.
    static func closeSpellings(of w: String, in spellings: [(word: String, letters: [UInt8])]) -> [String] {
        let a = Array(w.utf8)
        guard a.count >= 4, w.allSatisfy(\.isLetter) else { return [] }
        let k = a.count >= 7 ? 2 : 1
        return spellings.filter { startDistance(a, $0.letters, within: k) }.map(\.word)
    }

    /// Whether `a` is within `k` edits (insert, delete, change, or swap two neighbors) of some start of `b`: the optimal
    /// string alignment table, one column per letter of `b`, read at `a`'s end.
    static func startDistance(_ a: [UInt8], _ b: [UInt8], within k: Int) -> Bool {
        var before = Array(0...a.count), last = before, column = before
        for j in b.indices {
            column[0] = j + 1
            for i in 1...a.count {
                var d = min(last[i] + 1, column[i - 1] + 1, last[i - 1] + (a[i - 1] == b[j] ? 0 : 1))
                if i > 1, j > 0, a[i - 1] == b[j - 1], a[i - 2] == b[j] { d = min(d, before[i - 2] + 1) }
                column[i] = d
            }
            if column[a.count] <= k { return true }
            if column.min()! > k { return false }   // later columns only grow
            (before, last, column) = (last, column, before)
        }
        return false
    }

    static func normalized(_ s: String) -> String {
        s.folding(options: [.caseInsensitive, .diacriticInsensitive], locale: nil).trimmingCharacters(in: .whitespaces)
    }

    /// Folded words: "O'Connor" (or iOS's curly "O’Connor") and "St." stay one word ("oconnor", "st"); "&", "/", "-",
    /// "(" split ("Kedzie-Homan").
    static func words(_ s: String) -> [String] {
        normalized(s).replacingOccurrences(of: "['’‘.]", with: "", options: .regularExpression)
            .split { !$0.isLetter && !$0.isNumber }.map(String.init)
    }

    /// The query's words without joiners ("clark and belmont", "clark at belmont"), unless that's all there is ("and").
    static func queryWords(_ query: String) -> [String] {
        let all = words(query)
        let kept = all.filter { $0 != "and" && $0 != "at" }
        return kept.isEmpty ? all : kept
    }

    /// Trains' line names as words, for "red", "brown", "red line".
    static func lineWords(_ routes: [String]) -> [String] {
        routes.flatMap { words(LineColors.train($0)?.name ?? $0) }
    }

    /// One query word matches the start of a word in the name, a route exactly ("22" isn't "122"), or the start of a
    /// train line's name ("red", not "ed").
    static func matches(_ w: String, name: [String], routes: [String], lineWords: [String]) -> Bool {
        inName(w, name) || routes.contains(w) || lineWords.contains { $0.hasPrefix(w) }
    }

    /// The names say "North Ave", "Marquette Rd", "Skokie Blvd", "Forest Preserve Dr" (elsewhere "Avenue", "Drive"), so
    /// a typed long form also finds the short one. Short forms already prefix the long ones ("st" finds "Street").
    static let shortForms = ["avenue": "ave", "road": "rd", "boulevard": "blvd", "drive": "dr"]

    /// The start of any word in the name, in any order ("belm", "clark").
    static func inName(_ w: String, _ name: [String]) -> Bool {
        let short = shortForms[w]
        return name.contains { $0.hasPrefix(w) || $0 == short }
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
