import CoreLocation
import CTAKit
import Foundation

/// Find my handheld (ecosystem spec 5.9): where the phone was, and when, as a wearable dropped off both links.
/// Kept per device in the App Group; no coordinate means location was off or there was no fix yet.
nonisolated struct LastSeen: Codable, Hashable {
    var latitude: Double?
    var longitude: Double?
    var date: Date

    var coordinate: CLLocationCoordinate2D? {
        guard let latitude, let longitude else { return nil }
        return CLLocationCoordinate2D(latitude: latitude, longitude: longitude)
    }

    /// What the device page's Last seen section shows.
    enum Shown: Equatable { case connectedNow, seen(LastSeen), never }

    /// How long after the app comes to the foreground a `.wentOffline` is a drop from while it was away: 3 missed
    /// polls of `DeviceLink.pollSec` (2 s) plus slack.
    static let settleSec: TimeInterval = 3 * 2 + 2

    /// Only `.wentOffline` records (Bluetooth gone and 3 missed polls); `fix` is the phone's latest, if it has one.
    /// Not just after reopening: polling stops in the background, so that drop happened somewhere else, earlier.
    static func record(_ action: DeviceWatch.Action, fix: CLLocationCoordinate2D?, now: Date,
                       activeSince: Date) -> LastSeen? {
        guard action == .wentOffline, now.timeIntervalSince(activeSince) > settleSec else { return nil }
        return LastSeen(latitude: fix?.latitude, longitude: fix?.longitude, date: now)
    }

    static func shown(online: Bool, _ seen: LastSeen?) -> Shown {
        online ? .connectedNow : seen.map(Shown.seen) ?? .never
    }

    /// "Last seen 4:12 PM", with the day once it isn't today, and "· location unavailable" without a fix.
    func label(now: Date = .now, calendar: Calendar = .current) -> String {
        let today = calendar.isDate(date, inSameDayAs: now)
        let time = date.formatted(date: today ? .omitted : .abbreviated, time: .shortened)
        return "Last seen \(time)" + (coordinate == nil ? " · location unavailable" : "")
    }
}
