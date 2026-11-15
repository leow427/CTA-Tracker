import Foundation
import Security

/// The HTTP link's bodies (ecosystem spec 4.4). `POST /pin` sends a `ConfigStop`; `POST /config` a `ConfigMessage`.

public enum DeviceKind: String, Codable, Sendable { case dock, handheld }

public struct WifiState: Codable, Equatable, Sendable {
    public let connected: Bool
    public let ssid: String
    public init(connected: Bool, ssid: String) { self.connected = connected; self.ssid = ssid }
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
    enum CodingKeys: String, CodingKey {
        case type, id, fw, battery, charging, wifi, uptime, wake, docked
        case lastUpdate = "last_update", picksVer = "picks_ver"
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
