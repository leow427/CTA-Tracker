import Foundation

/// The JSON messages the app writes over BLE (base spec 10). Each must fit one 512-byte write.
public enum BLEMessage {
    /// Compact JSON, or nil if it's over 512 bytes.
    public static func encode<T: Encodable>(_ message: T) -> Data? {
        let e = JSONEncoder()
        e.outputFormatting = .withoutEscapingSlashes
        guard let data = try? e.encode(message), data.count <= Config.bleMaxPayload else { return nil }
        return data
    }
}

/// `wifi`: written only on an encrypted link. The password is never logged or stored on the phone,
/// so this is encode-only and its descriptions leave the password out.
public struct WifiMessage: Encodable, Sendable, CustomStringConvertible, CustomDebugStringConvertible, CustomReflectable {
    public let slot: Int   // 0–2; the dock only accepts 0
    public let ssid: String
    let pass: String
    public init(slot: Int, ssid: String, pass: String) { self.slot = slot; self.ssid = ssid; self.pass = pass }

    public var description: String { "WifiMessage(slot: \(slot), ssid: \(ssid), pass: <redacted>)" }
    public var debugDescription: String { description }
    public var customMirror: Mirror { Mirror(self, children: ["slot": slot, "ssid": ssid]) }
}

/// `nearby`: each list nearest first; `close` is the 3 nearest of either kind.
public struct NearbyMessage: Codable, Sendable {
    public let lat: Double
    public let lon: Double
    public let trains: [String]   // station IDs
    public let buses: [String]    // bus stop IDs
    public let close: [String]
    public init(lat: Double, lon: Double, trains: [String], buses: [String], close: [String]) {
        self.lat = lat; self.lon = lon; self.trains = trains; self.buses = buses; self.close = close
    }
}

/// `config`: a write may include any subset of keys; nil keys are left out. `stops` is dock-only.
public struct ConfigMessage: Codable, Sendable {
    public var stops: [ConfigStop]?
    public var pet: PetConfig?
    public var settings: DeviceSettings?
    public init(stops: [ConfigStop]? = nil, pet: PetConfig? = nil, settings: DeviceSettings? = nil) {
        self.stops = stops; self.pet = pet; self.settings = settings
    }
}

/// A picked stop (base spec 6.1), with fields cut to the dock's sizes.
public struct ConfigStop: Codable, Hashable, Sendable {
    public let type: StopRecord.RecordType
    public let route: String
    public let dir: String
    public let id: String
    public let name: String
    public init(type: StopRecord.RecordType, route: String, dir: String, id: String, name: String) {
        self.type = type
        self.route = route.truncatedUTF8(Config.idLen)
        self.dir = dir.truncatedUTF8(Config.nameLen)
        self.id = id.truncatedUTF8(Config.idLen)
        self.name = name.truncatedUTF8(Config.nameLen)
    }
}

public struct PetConfig: Codable, Hashable, Sendable {
    public let name: String
    public let colors: [UInt32]   // 0xRRGGBB for palette slots 2 and 3 (one preset from art/presets.txt)
    public init(name: String, colors: [UInt32]) { self.name = name.truncatedUTF8(Config.petNameLen); self.colors = colors }
}

public struct DeviceSettings: Codable, Hashable, Sendable {
    public let screen: Int       // brightness, percent
    public let led: Int          // brightness, percent (dock only)
    public let nightStart: Int   // minutes after midnight
    public let nightEnd: Int
    enum CodingKeys: String, CodingKey { case screen, led, nightStart = "night_start", nightEnd = "night_end" }
    public init(screen: Int, led: Int, nightStart: Int, nightEnd: Int) {
        self.screen = screen; self.led = led; self.nightStart = nightStart; self.nightEnd = nightEnd
    }
}
