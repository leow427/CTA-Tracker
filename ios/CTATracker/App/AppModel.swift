import CoreLocation
import CTAKit
import Observation
import SwiftUI

/// The app's root state: location, the list's center, the switch, and which cards the list shows (app spec 7.1).
@Observable final class AppModel {
    /// State and Madison: the map's start with no location (plan 4 decision 7).
    static let loop = CLLocationCoordinate2D(latitude: 41.8819, longitude: -87.6278)

    let index: StopIndex?
    let location = LocationService()

    /// The list follows this: the user's location, or wherever the map was panned to.
    var center: CLLocationCoordinate2D = AppModel.loop { didSet { rebuild() } }
    var filter: KindFilter = .all { didSet { rebuild() } }
    var favorites: [Favorite] = [] { didSet { rebuild() } }
    private(set) var slots = SlotList(favorites: [], nearby: [], farFromChicago: false)

    /// The gear on the map sets this; the sheet's content presents Settings (app spec 3).
    var showSettings = false
    /// The sheet's top edge on screen, so the map frames and follows only the part left visible.
    var sheetTop: CGFloat = .infinity

    init() {
        let url = Bundle.main.url(forResource: "stops", withExtension: "json")
        index = url.flatMap { try? Data(contentsOf: $0) }.flatMap { try? StopIndex(data: $0) }
        rebuild()
    }

    func stop(for slot: Slot) -> Stop? {
        index?.stop(id: slot.fetchID)
    }

    private func rebuild() {
        guard let index else { return }
        slots = CardBuilder.slots(center: center, filter: filter, favorites: favorites, index: index)
    }
}

/// When In Use location, as SwiftUI-friendly state.
@Observable final class LocationService: NSObject, CLLocationManagerDelegate {
    private let manager = CLLocationManager()
    private(set) var coordinate: CLLocationCoordinate2D?
    private(set) var status: CLAuthorizationStatus = .notDetermined

    var denied: Bool { status == .denied || status == .restricted }

    override init() {
        super.init()
        manager.delegate = self
        manager.desiredAccuracy = kCLLocationAccuracyHundredMeters
        manager.distanceFilter = 30   // a new fix only after moving 30 m: the list doesn't rebuild on GPS jitter
        status = manager.authorizationStatus
    }

    /// Asks once (first launch), then keeps the fix current while the app is in use.
    func start() {
        if manager.authorizationStatus == .notDetermined {
            manager.requestWhenInUseAuthorization()
        } else {
            manager.startUpdatingLocation()
        }
    }

    nonisolated func locationManagerDidChangeAuthorization(_ manager: CLLocationManager) {
        let status = manager.authorizationStatus
        Task { @MainActor in
            self.status = status
            if status == .authorizedWhenInUse || status == .authorizedAlways { self.manager.startUpdatingLocation() }
        }
    }

    nonisolated func locationManager(_ manager: CLLocationManager, didUpdateLocations locations: [CLLocation]) {
        guard let c = locations.last?.coordinate else { return }
        Task { @MainActor in self.coordinate = c }
    }

    nonisolated func locationManager(_ manager: CLLocationManager, didFailWithError error: Error) {}
}
