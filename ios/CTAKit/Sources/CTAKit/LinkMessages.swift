import Foundation
import Security

/// The HTTP link's bodies (ecosystem spec 4.4). `POST /pin` sends a `ConfigStop`; `POST /config` a `ConfigMessage`.

public enum DeviceKind: String, Codable, Sendable { case dock, handheld }

public struct WifiState: Codable, Equatable, Sendable {
    public let connected: Bool
    public let ssid: String
    public let rssi: Int   // dBm; 0 when not joined, or absent (the dock)
    public init(connected: Bool, ssid: String, rssi: Int = 0) { self.connected = connected; self.ssid = ssid; self.rssi = rssi }
    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        connected = try c.decode(Bool.self, forKey: .connected)
        ssid = try c.decode(String.self, forKey: .ssid)
        rssi = try c.decodeIfPresent(Int.self, forKey: .rssi) ?? 0
    }
}

/// `GET /info`. `uptime` (seconds since boot) lets the app spot a reboot.
public struct InfoMessage: Decodable, Equatable, Sendable {
    public let type: DeviceKind
    public let id: String
    public let fw: String
    public let battery: Int?       // percent; nil (JSON null) = no battery (the dock)
    public let charging: Bool
    public let wifi: WifiState
    public let lastUpdate: Int     // unix seconds of the last good fetch
    public let uptime: Int
    public let picksVer: Int       // dock: goes up on every pick
    public let wake: Int           // wearable: goes up on every wake
    public let docked: Bool        // wearable
    public let paired: Bool        // has a token; false (or absent) raises the app's "found" card
    public let relaying: Bool      // wearable: a Bluetooth /relay tick in the last 90 s; false when absent
    public let settings: DeviceSettings?   // wearable, the config.settings shape; nil when absent
    public let pin: String?        // wearable: the pinned stop's id; nil (JSON null, or absent: the dock) = none
    enum CodingKeys: String, CodingKey {
        case type, id, fw, battery, charging, wifi, uptime, wake, docked, paired, relaying, settings, pin
        case lastUpdate = "last_update", picksVer = "picks_ver"
    }
    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        type = try c.decode(DeviceKind.self, forKey: .type)
        id = try c.decode(String.self, forKey: .id)
        fw = try c.decode(String.self, forKey: .fw)
        battery = try c.decodeIfPresent(Int.self, forKey: .battery)
        charging = try c.decode(Bool.self, forKey: .charging)
        wifi = try c.decode(WifiState.self, forKey: .wifi)
        lastUpdate = try c.decode(Int.self, forKey: .lastUpdate)
        uptime = try c.decode(Int.self, forKey: .uptime)
        picksVer = try c.decode(Int.self, forKey: .picksVer)
        wake = try c.decode(Int.self, forKey: .wake)
        docked = try c.decode(Bool.self, forKey: .docked)
        paired = try c.decodeIfPresent(Bool.self, forKey: .paired) ?? false
        relaying = try c.decodeIfPresent(Bool.self, forKey: .relaying) ?? false
        settings = try c.decodeIfPresent(DeviceSettings.self, forKey: .settings)
        pin = try c.decodeIfPresent(String.self, forKey: .pin)
    }
}

/// `POST /relay`, every 30 s while the phone relays: its clock, and the weather when it has it.
public struct RelayTick: Encodable, Sendable {
    public let now: Int        // unix seconds
    public let wmo: Int?       // weather code; left out when nil
    public let temp: Double?   // °C; left out when nil
    public init(now: Int, wmo: Int?, temp: Double?) { self.now = now; self.wmo = wmo; self.temp = temp }
}

/// `POST /stop`: every record for one stop, built with `StopRecord.make`. Encode with
/// `BLEMessage.encode(_:limit: BLEFrames.maxMessage)`; over HTTP it must fit `Config.linkMaxBody`.
public struct RelayStop: Encodable, Sendable {
    public let id: String
    public let type: StopRecord.RecordType
    public let records: [StopRecord]
    public init(id: String, type: StopRecord.RecordType, records: [StopRecord]) {
        self.id = id.truncatedUTF8(Config.idLen)
        self.type = type
        self.records = Array(records.prefix(Config.maxRelayRecords))
    }
}

/// `POST /follow`: one stop plus the vehicle to count down to, encoded flat as `{type, route, dir, id, name, vehicle, dest}`.
public struct FollowMessage: Encodable, Sendable {
    public let stop: ConfigStop
    public let vehicle: String   // rn or vid
    public let dest: String
    public init(stop: ConfigStop, vehicle: String, dest: String) {
        self.stop = stop
        self.vehicle = vehicle.truncatedUTF8(Config.idLen)
        self.dest = dest.truncatedUTF8(Config.nameLen)
    }
    enum CodingKeys: String, CodingKey { case vehicle, dest }
    public func encode(to encoder: Encoder) throws {
        try stop.encode(to: encoder)
        var c = encoder.container(keyedBy: CodingKeys.self)
        try c.encode(vehicle, forKey: .vehicle)
        try c.encode(dest, forKey: .dest)
    }
}

public extension ConfigStop {
    /// What "Send to Trackie" pins (spec 5.6): a station by its own ID, as the wearable polls it, or a bus stop, with
    /// the card's route and direction label, keyed as the wearable keys its cards.
    init(pinning stop: Stop, line: Line) {
        self.init(type: stop.kind == .station ? .train : .bus, route: line.route, dir: line.dir, id: stop.id, name: stop.name)
    }
}

public extension FollowMessage {
    /// "Follow on Trackie" for one departure (spec 5.7); nil when the feed gave it no vehicle to match.
    init?(stop: Stop, line: Line, departure p: Prediction) {
        guard let vehicle = p.vehicle, !vehicle.isEmpty else { return nil }
        self.init(stop: ConfigStop(pinning: stop, line: line), vehicle: vehicle, dest: p.dest)
    }
}

/// `token`: written over the encrypted BLE link at the end of setup. Kept only in the Keychain,
/// so its descriptions leave the token out.
public struct TokenMessage: Encodable, Sendable, CustomStringConvertible, CustomDebugStringConvertible, CustomReflectable {
    let token: String
    public init(token: String) { self.token = token }

    public var description: String { "TokenMessage(token: <redacted>)" }
    public var debugDescription: String { description }
    public var customMirror: Mirror { Mirror(self, children: [:]) }
}

/// The household token: 128 random bits as 32 lowercase hex characters.
public enum Token {
    public static func random() -> String {
        var bytes = [UInt8](repeating: 0, count: 16)
        let status = SecRandomCopyBytes(kSecRandomDefault, bytes.count, &bytes)
        precondition(status == errSecSuccess, "SecRandomCopyBytes failed")   // never fall back to a weak token
        return bytes.map { String(format: "%02x", $0) }.joined()
    }
}
