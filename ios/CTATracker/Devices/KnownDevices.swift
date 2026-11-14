import Foundation

/// The devices set up from this app (app spec 7.4), saved as JSON in the App Group like the favorites.
enum KnownDevices {
    private static let key = "devices"
    private static var defaults: UserDefaults { UserDefaults(suiteName: AppModel.appGroup) ?? .standard }

    static func load() -> [DeviceLink.Device] {
        defaults.data(forKey: key).flatMap { try? JSONDecoder().decode([DeviceLink.Device].self, from: $0) } ?? []
    }

    static func save(_ devices: [DeviceLink.Device]) {
        if let data = try? JSONEncoder().encode(devices) { defaults.set(data, forKey: key) }
    }
}
