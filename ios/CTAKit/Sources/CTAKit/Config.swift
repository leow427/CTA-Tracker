import Foundation

/// Spec values, defined once. Mirrors firmware/lib/cta/src/cta_config.h (plan 1, task 2) plus the app's own.
public enum Config {
    public static let maxArrivals = 4           // per stop record
    public static let nameLen = 24              // bytes; names, dirs, and dests in a record
    public static let idLen = 7                 // bytes; ids and routes (char[8] on the devices)
    public static let petNameLen = 16           // bytes (petName[17] on the devices)
    public static let hypedMaxMin = 3
    public static let staleSec: TimeInterval = 300
    public static let nightStartMin = 60        // 1:00
    public static let nightEndMin = 300         // 5:00
    public static let bleMaxPayload = 512
    public static let nearbyPerTab = 10
    public static let hypedClosestStops = 3

    // App polling (base spec 6.6, app column)
    public static let listPollSec: TimeInterval = 30
    public static let livePollSec: TimeInterval = 30
    public static let alertsPollSec: TimeInterval = 300
    public static let weatherPollSec: TimeInterval = 900

    public static let cardDimSec: TimeInterval = 120   // cards dim after 2 min (app spec 8)
    public static let departedGraceSec: TimeInterval = 60   // an arrival this far past still reads "Due", then drops
    public static let busStopsPerCall = 10

    // The list (app spec 4.2 and 8)
    public static let cardRadiusM: Double = 800
    public static let fallbackStops = 5
    public static let farFromChicagoM: Double = 30_000
    public static let maxFavorites = 3
}

/// BLE UUIDs (base spec 10). Mirrors firmware/lib/cta/src/ble_ids.h.
public enum BLEID {
    public static let service = "d8d4388c-c8e3-4ff0-b269-015d0979c266"
    public static let info = "fb360799-3925-45c3-90be-c32d504bbc99"
    public static let wifi = "8d0820b1-1735-4f23-b1be-17b5d867632e"
    public static let config = "d8427173-107a-4c28-97ba-ab3b12d1500f"
    public static let request = "3c4dd75c-a019-4171-b7f1-b8dd87eda07c"
    public static let nearby = "e8b866e4-18bf-4ab4-bd7c-07b52cb2a057"
    public static let snapshot = "7d05cd5a-5784-431f-b051-deb65bc0c3ee"
}

/// CTA line colors (base spec 6.5). Mirrors firmware/lib/cta/src/line_colors.*.
public enum LineColors {
    public struct TrainLine: Sendable, Hashable {
        public let route: String   // Train Tracker / GTFS route ID
        public let name: String
        public let rgb: UInt32
    }

    public static let trains: [TrainLine] = [
        TrainLine(route: "Red", name: "Red Line", rgb: 0xC60C30),
        TrainLine(route: "Blue", name: "Blue Line", rgb: 0x00A1DE),
        TrainLine(route: "Brn", name: "Brown Line", rgb: 0x62361B),
        TrainLine(route: "G", name: "Green Line", rgb: 0x009B3A),
        TrainLine(route: "Org", name: "Orange Line", rgb: 0xF9461C),
        TrainLine(route: "Pink", name: "Pink Line", rgb: 0xE27EA6),
        TrainLine(route: "P", name: "Purple Line", rgb: 0x522398),
        TrainLine(route: "Y", name: "Yellow Line", rgb: 0xF9E300),
    ]
    public static let bus: UInt32 = 0x565A5C

    /// The train line for a route, case-insensitive (ttpositions writes "red"); nil for buses.
    public static func train(_ route: String) -> TrainLine? {
        trains.first { $0.route.caseInsensitiveCompare(route) == .orderedSame }
    }

    public static func rgb(_ route: String) -> UInt32 { train(route)?.rgb ?? bus }

    /// The app's navy panel and map (Transit-style restyle).
    public static let navy: UInt32 = 0x0B1A36
    /// Buses on navy: a bright blue, since the devices' gray and Ink vanish there.
    public static let busAccent: UInt32 = 0x2F7BFF

    /// A route's accent: the train's color, the bus blue for every bus.
    public static func accent(_ route: String) -> UInt32 { train(route)?.rgb ?? busAccent }

    /// The color as text on navy: mixed toward white in 10% steps until it reaches `minimum` (4.5:1 for text,
    /// 3:1 for large type), so Brown and Purple read and the big numbers keep their color.
    public static func onNavy(_ rgb: UInt32, minimum: Double = 4.5) -> UInt32 {
        var c = rgb
        for step in 1...10 where contrast(c, navy) < minimum {
            func mix(_ shift: UInt32) -> UInt32 {
                let v = Double(rgb >> shift & 0xFF)
                return UInt32((v + (255 - v) * Double(step) / 10).rounded()) << shift
            }
            c = mix(16) | mix(8) | mix(0)
        }
        return c
    }

    /// A stale card's fill: the line color halfway to the background's gray (systemGray5 in light, systemGray4 in dark),
    /// so it recedes in both modes. Opaque, so its text color still passes 4.5:1.
    public static func dimmed(_ rgb: UInt32, dark: Bool) -> UInt32 {
        let gray: UInt32 = dark ? 0x3A3A3C : 0xE5E5EA
        func mix(_ shift: UInt32) -> UInt32 { (((rgb >> shift & 0xFF) + (gray >> shift & 0xFF)) / 2) << shift }
        return mix(16) | mix(8) | mix(0)
    }

    /// White when it reaches WCAG 4.5:1 on the line color, black otherwise (plan 4 decision 6).
    public static func textColor(on rgb: UInt32) -> UInt32 {
        contrast(0xFFFFFF, rgb) >= 4.5 ? 0xFFFFFF : 0x000000
    }

    /// WCAG 2 contrast ratio between two sRGB colors.
    public static func contrast(_ a: UInt32, _ b: UInt32) -> Double {
        let (la, lb) = (luminance(a), luminance(b))
        return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)
    }

    static func luminance(_ rgb: UInt32) -> Double {
        func channel(_ shift: UInt32) -> Double {
            let c = Double((rgb >> shift) & 0xFF) / 255
            return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4)
        }
        return 0.2126 * channel(16) + 0.7152 * channel(8) + 0.0722 * channel(0)
    }
}
