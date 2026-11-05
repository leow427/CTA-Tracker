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
    /// Starred cards (plan 4 decision 1), saved as JSON in the App Group (app spec 7.4) so the widget can read them.
    var favorites: [Favorite] = AppModel.loadFavorites() {
        didSet { rebuild(); AppModel.save(favorites) }
    }
    /// The stop whose detail is open, so the map can center on it (app spec 3: the map follows the list).
    var detailStop: Stop?
    private(set) var slots = SlotList(favorites: [], nearby: [], farFromChicago: false)

    /// The gear on the map sets this; the card list presents Settings (app spec 3).
    var showSettings = false
    /// The card list's top edge on screen: touches above it go to the map (redesign spec 2).
    var listTop: CGFloat = .infinity
    /// Stop detail's content top on screen, the same for detail.
    var detailTop: CGFloat = .infinity

    init() {
        let url = Bundle.main.url(forResource: "stops", withExtension: "json")
        index = url.flatMap { try? Data(contentsOf: $0) }.flatMap { try? StopIndex(data: $0) }
        rebuild()
    }

    func stop(for slot: Slot) -> Stop? {
        index?.stop(id: slot.fetchID)
    }

    func isFavorite(_ f: Favorite) -> Bool { favorites.contains(f) }

    /// ★ adds or removes a card. Not capped: the ★ section shows 3 and the rest show under ★ in search (app spec 2).
    func toggleFavorite(_ f: Favorite) {
        if let i = favorites.firstIndex(of: f) { favorites.remove(at: i) } else { favorites.append(f) }
    }

    /// Every favorite that still resolves to a card, for search.
    var allFavoriteSlots: [Slot] {
        index.map { CardBuilder.resolved(favorites: favorites, center: center, index: $0) } ?? []
    }

    /// Stop names folded once for search.
    @ObservationIgnored private(set) lazy var search: StopSearch.Index? = index.map { StopSearch.Index($0) }

    // MARK: - Storage

    static let appGroup = "group.io.github.leow427.CTATracker"
    private static let favoritesKey = "favorites"
    private static var defaults: UserDefaults { UserDefaults(suiteName: appGroup) ?? .standard }

    private static func loadFavorites() -> [Favorite] {
        defaults.data(forKey: favoritesKey).flatMap { try? JSONDecoder().decode([Favorite].self, from: $0) } ?? []
    }

    private static func save(_ favorites: [Favorite]) {
        if let data = try? JSONEncoder().encode(favorites) { defaults.set(data, forKey: favoritesKey) }
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
