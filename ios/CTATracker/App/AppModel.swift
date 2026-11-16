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
    /// The dock and wearable on this Wi-Fi (ecosystem spec 4.2), polled only while the app is active.
    let devices = DeviceLink()
    @ObservationIgnored private var lifecycle: [NSObjectProtocol] = []
    /// At most one mirrored `nearby` per 10 s (spec 5.5); `flushNearby` sends a held one once it's due.
    @ObservationIgnored private var throttle = NearbyThrottle()
    @ObservationIgnored private var flushNearby: Task<Void, Never>?
    /// Wearables that asked for `nearby` before the first fix; they get it when one arrives.
    @ObservationIgnored private var awaitingFix: Set<String> = []
    /// In the background the wearable keeps its last list (spec 5.5): Bluetooth notifies still arrive, but go unanswered.
    @ObservationIgnored private var active = false

    /// The list follows this: the user's location, or wherever the map was panned to.
    var center: CLLocationCoordinate2D = AppModel.loop { didSet { rebuild(); mirror() } }
    var filter: KindFilter = .all { didSet { rebuild(); mirror() } }
    /// The map follows the user's location; a pan clears it and the recenter button sets it again.
    var following = true {
        didSet { if following && !oldValue { recenter() } }
    }
    /// Starred cards (plan 4 decision 1), saved as JSON in the App Group (app spec 7.4) so the widget can read them.
    var favorites: [Favorite] = AppModel.loadFavorites() {
        didSet { rebuild(); AppModel.save(favorites); mirror() }
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
    /// What the map shows big over open stop detail: the chosen line and its stop (for ★).
    var detailHero: DetailHero?
    /// The red × over the map bumps this; the list pops its detail.
    var closeDetail = 0
    /// A stop icon tapped on the map; the list opens its detail and clears this.
    var openStop: String?
    /// The line to open `openStop` on, when it isn't the listed card's (a bus page's direction switch).
    var openLine: Line?

    init() {
        let url = Bundle.main.url(forResource: "stops", withExtension: "json")
        index = url.flatMap { try? Data(contentsOf: $0) }.flatMap { try? StopIndex(data: $0) }
        let stops = index
        searchIndex = Task.detached(priority: .utility) { stops.map(StopSearch.Index.init) }
        rebuild()
        devices.onAction = { [weak self] id, action in
            guard let self, isWearable(id) else { return }
            // Docking clears the wearable's pin (spec 5.6), so the app forgets it too.
            if action == .docked { (pinned, followed) = (nil, nil) }
            // A rebooted or newly seen wearable lost its pin and follow (kept in RAM, spec 5.6): give them back, even
            // from the background, since a Bluetooth reconnect can land there.
            if action == .resendState { resendPin(to: id) }
            guard active, action == .sendNearby || action == .cameOnline else { return }
            // A wake with the stops it already has sends nothing; coming online always does (it may have been reset).
            sendNearby(to: id, unlessHas: action == .sendNearby)
        }
        // A fresh pairing means a fresh wearable (a demo reset, spec 5.4): its old pin and follow don't come back.
        devices.onAdded = { [weak self] id in
            guard let self, isWearable(id) else { return }
            (pinned, followed) = (nil, nil)
            // Its first `.cameOnline` came before `add`, when it wasn't ours yet, so its stops go now.
            if active { sendNearby(to: id, unlessHas: false) }
        }
        location.onFix = { [weak self] in
            guard let self, following else { return }
            awaitingFix.filter { self.devices.online[$0] != nil }.forEach { self.sendNearby(to: $0, unlessHas: false) }
        }
        let nc = NotificationCenter.default
        lifecycle = [(UIApplication.didBecomeActiveNotification, true), (UIApplication.willResignActiveNotification, false)]
            .map { name, active in
                nc.addObserver(forName: name, object: nil, queue: .main) { [weak self] _ in
                    MainActor.assumeIsolated {
                        self?.active = active
                        self?.devices.setActive(active)
                        // A wearable that came online while the app was away got nothing (actions wait for the
                        // foreground): send now, or the relay never starts until its next wake.
                        if active { self?.onlineWearables.forEach { self?.sendNearby(to: $0, unlessHas: true) } }
                    }
                }
            }
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

    /// Every L station, for the map's icons.
    @ObservationIgnored private(set) lazy var stations: [Stop] = index?.stops.filter { $0.kind == .station } ?? []

    /// Stop names folded once for search, off the main thread from launch, so focusing search never waits on it.
    @ObservationIgnored private var searchIndex: Task<StopSearch.Index?, Never>?
    var search: StopSearch.Index? { get async { await searchIndex?.value } }

    // MARK: - Nearby on the wrist (ecosystem spec 5.5)

    /// Only wearables set up from this app: a stranger's (or a reset one) found over Bonjour never gets our token.
    private func isWearable(_ id: String) -> Bool {
        devices.devices.first { $0.id == id }?.kind == .handheld
    }

    var onlineWearables: [String] {
        devices.devices.filter { $0.kind == .handheld && devices.online[$0.id] != nil }.map(\.id)
    }

    /// The phone's card list goes to the wrist (spec 5.5): while following, the list at the fix (nil until one comes);
    /// while panned, the list on screen.
    private var mapNearby: NearbyMessage? {
        guard let index else { return nil }
        guard following else { return NearbyBuilder.fromList(slots, center: center) }
        return location.coordinate.map {
            NearbyBuilder.fromList(CardBuilder.slots(center: $0, filter: filter, favorites: favorites, index: index), center: $0)
        }
    }

    /// The wearable asked (a wake, or it came online) or recenter: send now, around the throttle. Following with no fix
    /// yet waits for one; with location off nothing goes and the wearable keeps its last list. `unlessHas` skips a
    /// wearable that already has these stops: each new list blanks its cards and re-polls.
    private func sendNearby(to id: String, unlessHas: Bool) {
        guard let m = mapNearby else { awaitingFix.insert(id); return }
        awaitingFix.remove(id)
        throttle.sent(m, now: .now)   // also drops a held pan offer, which would overwrite this
        if unlessHas, let had = devices.sentNearby[id], [had.trains, had.buses, had.close] == [m.trains, m.buses, m.close] {
            return
        }
        post(m, to: [id])
    }

    /// Recenter sends the user's own stops. Closing stop detail also restores following, hence `unlessHas`.
    private func recenter() {
        flushNearby?.cancel()
        onlineWearables.forEach { sendNearby(to: $0, unlessHas: true) }
    }

    /// While panned, the list's stops go to the wrist once it settles: at most every 10 s, only when they changed.
    /// Not while stop detail is open: its focus clears `following` without a pan.
    private func mirror() {
        guard !following, detailStop == nil, let m = mapNearby else { return }
        if let now = throttle.offer(m, now: .now) { post(now, to: onlineWearables); return }
        flushNearby?.cancel()
        flushNearby = Task { [weak self] in
            try? await Task.sleep(for: .seconds(NearbyThrottle.minInterval))
            guard !Task.isCancelled, let self, !following, detailStop == nil,
                  let m = throttle.flush(now: .now) else { return }
            post(m, to: onlineWearables)
        }
    }

    // shortcut: a failed post still counts as sent in the throttle; the wearable's next wake asks again.
    private func post(_ m: NearbyMessage, to ids: [String]) {
        for id in ids { Task { try? await devices.post("/nearby", m, to: id) } }
    }

    // MARK: - Send to Trackie and Follow (ecosystem spec 5.6, 5.7)

    /// What the wrist was last told to pin and follow, re-sent when it reboots or first shows up (`.resendState`).
    private(set) var pinned: ConfigStop?
    private(set) var followed: FollowMessage?
    @ObservationIgnored private var followedAt = Date.distantPast   // the followed departure's time

    /// Pins the stop on the online wearables; true when one took it. A pin of another stop ends the follow, as on
    /// the wearable.
    func pin(_ stop: ConfigStop) async -> Bool {
        guard await sendToWrist("/pin", stop) else { return false }
        (pinned, followed) = (stop, Self.follow(after: stop, followed: followed, followedAt: followedAt, now: .now))
        return true
    }

    /// A pin of the followed stop keeps the follow; any other pin ends it, as on the wearable. A follow past its
    /// grace is dropped too, or the next resend would clear the new pin with it.
    nonisolated static func follow(after pin: ConfigStop, followed: FollowMessage?, followedAt: Date,
                                   now: Date) -> FollowMessage? {
        guard followed?.stop == pin, now.timeIntervalSince(followedAt) < Config.followEndedSec else { return nil }
        return followed
    }

    /// Follows one departure on the online wearables (it pins its stop too); true when one took it.
    func follow(_ f: FollowMessage, at: Date) async -> Bool {
        guard await sendToWrist("/follow", f) else { return false }
        (pinned, followed, followedAt) = (f.stop, f, at)
        return true
    }

    private func sendToWrist(_ path: String, _ body: some Encodable & Sendable) async -> Bool {
        var ok = false
        for id in onlineWearables {
            do { try await devices.post(path, body, to: id); ok = true } catch {}
        }
        return ok
    }

    /// Nothing to send means nothing for the app to keep either (it was reset or docked, or there was no pin).
    private func resendPin(to id: String) {
        let info = devices.online[id]
        switch Self.resend(followed: followed, followedAt: followedAt, pinned: pinned,
                           paired: info?.paired ?? false, docked: info?.docked ?? false, now: .now) {
        case .follow(let f): Task { try? await devices.post("/follow", f, to: id) }
        case .pin(let p): Task { try? await devices.post("/pin", p, to: id) }
        case nil: (pinned, followed) = (nil, nil)
        }
    }

    enum Resend { case follow(FollowMessage), pin(ConfigStop) }

    /// The follow until `followEndedSec` past its departure (the wearable shows Arrived that long); after that nothing, as the wearable clears a passed follow with its pin (Leo 2026-11-15). A pin with no
    /// follow is resent. Nothing for a wearable that's reset (unpaired) or docked: it dropped its pin on purpose
    /// (spec 5.4, 5.6).
    nonisolated static func resend(followed: FollowMessage?, followedAt: Date, pinned: ConfigStop?,
                                   paired: Bool, docked: Bool, now: Date) -> Resend? {
        guard paired, !docked else { return nil }
        if let followed { return now.timeIntervalSince(followedAt) < Config.followEndedSec ? .follow(followed) : nil }
        return pinned.map { .pin($0) }
    }

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

/// The line stop detail is showing, drawn big over the map with ★ and × beside it.
struct DetailHero: Equatable {
    let line: Line
    let kind: Kind
    let stopID: String   // the group's platform or bus stop, which ★ saves
}
