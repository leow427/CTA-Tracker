import Foundation

/// A stop's kind, as `data/stops.json` writes it.
public enum Kind: String, Codable, Sendable { case bus, station }

/// The list's All · Trains · Buses switch.
public enum KindFilter: Sendable { case all, trains, buses }

/// A route and direction: one card. `dir` is Bus Tracker's rtdir, or the train line's direction label.
public struct Line: Codable, Hashable, Sendable {
    public let route: String
    public let dir: String
    public init(route: String, dir: String) { self.route = route; self.dir = dir }
}

/// A train platform inside its station, with the lines that stop there.
public struct Platform: Codable, Hashable, Sendable {
    public let id: String
    public let lines: [Line]
    public init(id: String, lines: [Line]) { self.id = id; self.lines = lines }
}

/// One entry of `data/stops.json`: a bus stop (`lines`) or a station (`platforms`).
public struct Stop: Codable, Hashable, Identifiable, Sendable {
    public let id: String
    public let name: String
    public let lat: Double
    public let lon: Double
    public let kind: Kind
    public let lines: [Line]?
    public let platforms: [Platform]?
    public init(id: String, name: String, lat: Double, lon: Double, kind: Kind, lines: [Line]?, platforms: [Platform]?) {
        self.id = id; self.name = name; self.lat = lat; self.lon = lon; self.kind = kind
        self.lines = lines; self.platforms = platforms
    }
}

/// A starred card: its stop (bus stop or platform ID) and line (plan 4 decision 1).
public struct Favorite: Codable, Hashable, Sendable {
    public let stopID: String
    public let line: Line
    public init(stopID: String, line: Line) { self.stopID = stopID; self.line = line }
}

/// One arrival from a parser: one per `eta` (trains) or `prd` (buses), uncapped.
public struct Prediction: Hashable, Sendable {
    public let stopID: String   // platform ID (ttarrivals stpId) or bus stop ID (stpid)
    public let route: String    // rt
    public let dir: String      // rtdir for buses; empty for trains (the platform implies the direction)
    public let dest: String
    public let at: Date
    public let live: Bool       // false = timetable only
    public let delayed: Bool

    public init(stopID: String, route: String, dir: String, dest: String, at: Date, live: Bool, delayed: Bool) {
        self.stopID = stopID; self.route = route; self.dir = dir; self.dest = dest
        self.at = at; self.live = live; self.delayed = delayed
    }
}

public enum Mood: String, Sendable, CaseIterable { case sleepy, sad, worried, happy, hyped }

public enum Accessory: Sendable { case none, umbrella, scarf }

/// A Customer Alerts item, kept for the ⚠ badge and the stop detail text.
/// Not named `Alert`, which would clash with SwiftUI's.
public struct ServiceAlert: Hashable, Sendable {
    public let id: String
    public let headline: String
    public let shortText: String
    public let fullText: String     // HTML from the API
    public let routes: [String]     // ServiceIds of type R (train) or B (bus)
}

/// A train (ttpositions) or bus (getvehicles) on the live map.
public struct Vehicle: Identifiable, Hashable, Sendable {
    public let id: String           // rn or vid
    public let lat: Double
    public let lon: Double
    public let heading: Double
    public let dest: String
    public let nextStopID: String   // trains only; empty for buses
    public let nextStop: String     // trains only; empty for buses
    public let pid: Int?            // buses only
    public let pdist: Double?       // buses only: feet along the pattern
}

/// A bus route pattern (getpatterns): the drawn path, and stops with distance along it.
public struct Pattern: Hashable, Sendable {
    public struct Point: Hashable, Sendable {
        public let lat: Double
        public let lon: Double
        public let stopID: String?   // nil for waypoints
        public let stopName: String?
        public let pdist: Double
    }
    public let pid: Int
    public let dir: String
    public let points: [Point]
}

public enum CTAError: Error, Equatable, Sendable {
    /// The API answered with an error instead of data (bad key, over the limit, …).
    case api(code: String, message: String)
}
