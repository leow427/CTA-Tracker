import Combine
import CoreLocation
import MapboxMaps
import Observation

/// When In Use location, as SwiftUI-friendly state, and the app's only CLLocationManager: it also feeds the map's
/// puck (`puck`), which otherwise ran a second manager at best accuracy.
@Observable final class LocationService: NSObject, CLLocationManagerDelegate {
    private let manager = CLLocationManager()
    /// The fix the list follows. It moves only after 30 m, so the list doesn't rebuild on GPS jitter; the puck gets
    /// every fix.
    private(set) var coordinate: CLLocationCoordinate2D?
    private(set) var status: CLAuthorizationStatus = .notDetermined

    var denied: Bool { status == .denied || status == .restricted }

    /// Every fix and compass heading, for the map's puck and follow camera (delivered on the main thread). One
    /// instance: Mapbox swaps its data model whenever a different one is passed.
    @ObservationIgnored private(set) lazy var puck = LocationDataModel(
        location: fixes.compactMap { $0 }.eraseToAnyPublisher(),
        heading: headings.compactMap { $0 }.eraseToAnyPublisher())
    // Current-value subjects, so a map that subscribes late still gets the last fix while the user stands still.
    @ObservationIgnored private let fixes = CurrentValueSubject<[Location]?, Never>(nil)
    @ObservationIgnored private let headings = CurrentValueSubject<Heading?, Never>(nil)
    /// The app is in the foreground: updates run only then.
    @ObservationIgnored private var active = false

    /// How far the user moves before `coordinate` (and so the list) follows.
    static let listMoveMeters: CLLocationDistance = 30

    override init() {
        super.init()
        manager.delegate = self
        // Fine enough for a smooth puck; the list still moves only every 30 m (`listMoveMeters`).
        manager.desiredAccuracy = kCLLocationAccuracyNearestTenMeters
        manager.distanceFilter = 5
        // 5° steps, not Mapbox's 1°: each heading redraws the tilted map. The app is portrait-only (Info.plist), and
        // Mapbox doesn't turn a custom heading for the interface orientation.
        manager.headingFilter = 5
        manager.headingOrientation = .portrait
        status = manager.authorizationStatus
    }

    /// Runs updates in the foreground and stops them in the background. Asks for permission once (first launch).
    func setActive(_ active: Bool) {
        self.active = active
        if !active {
            manager.stopUpdatingLocation()
            manager.stopUpdatingHeading()
        } else if manager.authorizationStatus == .notDetermined {
            manager.requestWhenInUseAuthorization()
        } else {
            startUpdates()
        }
    }

    private func startUpdates() {
        manager.startUpdatingLocation()
        if CLLocationManager.headingAvailable() { manager.startUpdatingHeading() }
    }

    nonisolated func locationManagerDidChangeAuthorization(_ manager: CLLocationManager) {
        let status = manager.authorizationStatus
        Task { @MainActor in
            self.status = status
            if active, status == .authorizedWhenInUse || status == .authorizedAlways { startUpdates() }
        }
    }

    nonisolated func locationManager(_ manager: CLLocationManager, didUpdateLocations locations: [CLLocation]) {
        guard let last = locations.last else { return }
        let accuracy = manager.accuracyAuthorization
        Task { @MainActor in
            fixes.send(locations.map { Self.mapLocation($0, accuracy: accuracy) })
            let moved = coordinate.map {
                CLLocation(latitude: $0.latitude, longitude: $0.longitude).distance(from: last) >= Self.listMoveMeters
            } ?? true
            if moved { coordinate = last.coordinate }
        }
    }

    nonisolated func locationManager(_ manager: CLLocationManager, didUpdateHeading newHeading: CLHeading) {
        let heading = Heading(from: newHeading)
        Task { @MainActor in headings.send(heading) }
    }

    nonisolated func locationManager(_ manager: CLLocationManager, didFailWithError error: Error) {}

    /// A fix for Mapbox: invalid (negative) readings left out, and the accuracy authorization under the key its puck
    /// reads (as its own provider sets it), so reduced accuracy still draws as approximate.
    nonisolated private static func mapLocation(_ l: CLLocation, accuracy: CLAccuracyAuthorization) -> Location {
        func valid(_ v: Double) -> Double? { v >= 0 ? v : nil }
        return Location(coordinate: l.coordinate, timestamp: l.timestamp, altitude: l.altitude,
                        horizontalAccuracy: valid(l.horizontalAccuracy), verticalAccuracy: valid(l.verticalAccuracy),
                        speed: valid(l.speed), speedAccuracy: valid(l.speedAccuracy), bearing: valid(l.course),
                        bearingAccuracy: valid(l.courseAccuracy), floor: l.floor?.level,
                        extra: ["accuracyAuthorization": NSNumber(value: accuracy.rawValue)])
    }
}
