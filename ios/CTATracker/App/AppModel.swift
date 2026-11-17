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
    var favorites: [Favorite] = AppModel.load(AppModel.favoritesKey) ?? [] {
        didSet { rebuild(); AppModel.save(favorites, AppModel.favoritesKey); mirror() }
    }
    /// The one Trackie (ecosystem spec 5.2): its name and colors, kept in the App Group and written to every device.
    var pet: PetConfig = AppModel.load(AppModel.petKey) ?? Pet.defaultConfig {
        didSet { Glyphs.petColors = pet.colors; AppModel.save(pet, AppModel.petKey); pushConfig() }
    }
    /// Night hours, shared by every device (spec 5.2, Settings layout); copied into each device's `settings`.
    var quietHours: QuietHours = AppModel.load(AppModel.quietKey) ?? QuietHours(AppModel.legacySettings) {
        didSet { AppModel.save(quietHours, AppModel.quietKey); pushConfig() }
    }
    /// Each device's own brightness, by device id; a device with none uses Task 13's single settings.
    private(set) var devicePrefs: [String: DevicePrefs] = AppModel.load(AppModel.prefsKey) ?? [:] {
        didSet { AppModel.save(devicePrefs, AppModel.prefsKey) }
    }
    /// Where and when each wearable last dropped off both links (spec 5.9), for its device page.
    private(set) var lastSeen: [String: LastSeen] = AppModel.load(AppModel.lastSeenKey) ?? [:] {
        didSet { AppModel.save(lastSeen, AppModel.lastSeenKey) }
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
        Glyphs.petColors = pet.colors
        devices.onInfo = { [weak self] id, info in self?.adoptDeviceSettings(id, info.settings) }
        devices.onAction = { [weak self] id, action in
            guard let self else { return }
            if action == .fetchConfig, isDock(id) { fetchConfig(id) }
            // `.resendState` comes with every `.cameOnline` and on a reboot, so the pet and settings go once for each.
            if action == .resendState, isPaired(id) { sendConfig(to: id) }
            guard isWearable(id) else { return }
            if let seen = LastSeen.record(action, fix: location.latest, now: .now) { lastSeen[id] = seen }
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
            guard let self else { return }
            // Its first `.fetchConfig` and `.resendState` came before `add`, when it wasn't ours yet.
            sendConfig(to: id)
            if isDock(id) { fetchConfig(id) }
            guard isWearable(id) else { return }
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

    // MARK: - Dock stops (ecosystem spec 5.3)

    /// Each paired dock's 3 sections as it last reported them (`GET /config`), read on `.fetchConfig`.
    private(set) var dockPicks: [String: [DockConfig.Pick]] = [:]
    @ObservationIgnored private var configFetches: [String: Task<Void, Never>] = [:]

    private func isDock(_ id: String) -> Bool { devices.devices.first { $0.id == id }?.kind == .dock }

    /// Paired docks online whose sections are known: what "Send to dock" can replace.
    var onlineDocks: [String] {
        devices.devices.filter { $0.kind == .dock && devices.online[$0.id] != nil && dockPicks[$0.id] != nil }.map(\.id)
    }

    /// Reads the dock's sections; a missed poll is normal (DeviceWatch allows 3), so it retries while the dock is online.
    private func fetchConfig(_ id: String) {
        configFetches[id]?.cancel()
        configFetches[id] = Task { [weak self] in
            while let self, !Task.isCancelled, devices.online[id] != nil {
                if let config: DockConfig = try? await devices.get("/config", from: id), let stops = config.stops,
                   !Task.isCancelled {
                    dockPicks[id] = DockConfig.padded(stops)
                    return
                }
                try? await Task.sleep(for: .seconds(DeviceLink.pollSec))
            }
        }
    }

    /// Replaces one section of the dock, keeping the other two as the dock has them now; true when it took it.
    /// The dock raises `picks_ver`, and the `.fetchConfig` that follows reads back what it saved.
    func sendToDock(_ stop: ConfigStop, section: Int, dock id: String) async -> Bool {
        // Read fresh, not dockPicks: a pick just made on the dock must not be overwritten (spec 5.3). No read, no
        // send: never blank the other two sections.
        guard let fresh: DockConfig = try? await devices.get("/config", from: id), let stops = fresh.stops else { return false }
        let current = DockConfig.padded(stops)
        let picks = DockConfig.replacing(current, section: section, with: stop)
        do { try await devices.post("/config", DockConfig(stops: picks), to: id) } catch { return false }
        dockPicks[id] = picks
        return true
    }

    // MARK: - One Trackie and settings per device (ecosystem spec 5.2)

    /// The boards' own defaults: full brightness, night 1:00–5:00.
    static let defaultSettings = DeviceSettings(screen: 100, led: 100, nightStart: Config.nightStartMin,
                                                nightEnd: Config.nightEndMin)
    /// Task 13 saved one `DeviceSettings` for all devices: it's every device's default now, so nothing is lost.
    private static let legacySettings: DeviceSettings = load(settingsKey) ?? defaultSettings
    @ObservationIgnored private var configPush: Task<Void, Never>?
    @ObservationIgnored private var pushTo: Set<String>? = []   // nil: every device

    private func isPaired(_ id: String) -> Bool { devices.devices.contains { $0.id == id } }

    func prefs(_ id: String) -> DevicePrefs { devicePrefs[id] ?? DevicePrefs(AppModel.legacySettings) }

    /// One device's brightness changed: only it gets the write.
    func setPrefs(_ p: DevicePrefs, for id: String) {
        devicePrefs[id] = p
        pushConfig(to: id)
    }

    /// What one device is sent: its own brightness with the shared quiet hours. Off is start == end, which both
    /// boards read as no night (`cta::inNightHours`, the wearable's `Model::night`).
    nonisolated static func settings(_ p: DevicePrefs, quiet: QuietHours) -> DeviceSettings {
        DeviceSettings(screen: p.screen, led: p.ledOn ? p.led : 0, nightStart: quiet.start,
                       nightEnd: quiet.on ? quiet.end : quiet.start)
    }

    /// A slider or the name field changes many times a second: write once it settles. `id` nil writes every device.
    private func pushConfig(to id: String? = nil) {
        if let id { pushTo?.insert(id) } else { pushTo = nil }
        configPush?.cancel()
        configPush = Task { [weak self] in
            try? await Task.sleep(for: .milliseconds(400))
            guard let self, !Task.isCancelled else { return }
            let to = pushTo
            pushTo = []   // nothing waiting: configPending is false again
            devices.devices.filter { devices.online[$0.id] != nil && to?.contains($0.id) != false }
                .forEach { sendConfig(to: $0.id) }
        }
    }

    // shortcut: a failed write isn't retried; the device gets it again on its next `.resendState` (online or reboot).
    private func sendConfig(to id: String) {
        let s = Self.settings(prefs(id), quiet: quietHours)
        sending[id, default: 0] += 1
        Task {
            if (try? await devices.post("/config", ConfigMessage(pet: pet, settings: s), to: id)) != nil { sentSettings[id] = s }
            sending[id, default: 1] -= 1
        }
    }

    // MARK: - A change made on the device (ecosystem spec 5.2, "On the devices too")

    /// What each device has as far as the app knows: what it last took from the app, or adopted from it. Kept across
    /// launches, so a change made on the device while the app was closed is seen, and one made in the app isn't lost.
    @ObservationIgnored private var sentSettings: [String: DeviceSettings] = AppModel.load(AppModel.sentKey) ?? [:] {
        didSet { AppModel.save(sentSettings, AppModel.sentKey) }
    }
    @ObservationIgnored private var reportedSettings: [String: DeviceSettings] = [:]   // each device's last info.settings
    @ObservationIgnored private var sending: [String: Int] = [:]   // `/config` writes still in flight

    /// The app's own change for the device is waiting for the debounce or still on its way.
    private func configPending(_ id: String) -> Bool { (pushTo?.contains(id) ?? true) || sending[id, default: 0] > 0 }

    private func adoptDeviceSettings(_ id: String, _ reported: DeviceSettings?) {
        guard let reported, isPaired(id) else { return }
        defer { reportedSettings[id] = reported }
        let sent = sentSettings[id] ?? Self.settings(prefs(id), quiet: quietHours)
        guard let p = Self.adopted(reported, previous: reportedSettings[id], sent: sent, prefs: prefs(id),
                                   pending: configPending(id)) else { return }
        devicePrefs[id] = p   // no write back: the device has it already, and a `.resendState` now sends the same
        sentSettings[id] = reported
    }

    /// The device's own brightness (it owns it): the prefs to take when its reported screen or LED differs from what the
    /// app sent and changed since its last report. nil keeps the app's: its own change is still going out, nothing
    /// differs, or the report is a stale one from before the app's write landed. Quiet hours stay the app's.
    nonisolated static func adopted(_ reported: DeviceSettings, previous: DeviceSettings?, sent: DeviceSettings,
                                    prefs: DevicePrefs, pending: Bool) -> DevicePrefs? {
        func same(_ a: DeviceSettings, _ b: DeviceSettings) -> Bool { a.screen == b.screen && a.led == b.led }
        if pending || same(reported, sent) { return nil }
        if let previous, same(reported, previous) { return nil }
        var p = prefs
        p.screen = reported.screen
        if reported.led > 0 { p.led = reported.led }   // Off keeps the level, for turning the LEDs back on
        p.ledOn = reported.led > 0
        return p
    }

    // MARK: - Storage

    static let appGroup = "group.io.github.leow427.CTATracker"
    private static let favoritesKey = "favorites"
    private static let petKey = "pet"
    private static let settingsKey = "deviceSettings"   // Task 13's single settings, read for defaults only
    private static let quietKey = "quietHours"
    private static let prefsKey = "devicePrefs"
    private static let sentKey = "sentSettings"
    private static let lastSeenKey = "lastSeen"
    private static var defaults: UserDefaults { UserDefaults(suiteName: appGroup) ?? .standard }

    private static func load<T: Decodable>(_ key: String) -> T? {
        defaults.data(forKey: key).flatMap { try? JSONDecoder().decode(T.self, from: $0) }
    }

    private static func save(_ value: some Encodable, _ key: String) {
        if let data = try? JSONEncoder().encode(value) { defaults.set(data, forKey: key) }
    }

    private func rebuild() {
        guard let index else { return }
        slots = CardBuilder.slots(center: center, filter: filter, favorites: favorites, index: index)
    }
}

/// One device's own controls (spec 5.2, Settings layout). `led` is kept while the LEDs are off, for turning them on.
nonisolated struct DevicePrefs: Codable, Hashable {
    var screen: Int
    var led: Int
    var ledOn: Bool

    init(screen: Int = 100, led: Int = 100, ledOn: Bool = true) { (self.screen, self.led, self.ledOn) = (screen, led, ledOn) }
    init(_ s: DeviceSettings) { self.init(screen: s.screen, led: s.led > 0 ? s.led : 100, ledOn: s.led > 0) }
}

/// Night hours for every device, in minutes after midnight; the times are kept while they're off.
nonisolated struct QuietHours: Codable, Hashable {
    var on: Bool
    var start: Int
    var end: Int

    init(on: Bool = true, start: Int = Config.nightStartMin, end: Int = Config.nightEndMin) {
        (self.on, self.start, self.end) = (on, start, end)
    }
    /// Task 13 had no switch: start == end meant none.
    init(_ s: DeviceSettings) {
        if s.nightStart == s.nightEnd { self.init(on: false) } else { self.init(start: s.nightStart, end: s.nightEnd) }
    }
}

/// The line stop detail is showing, drawn big over the map with ★ and × beside it.
struct DetailHero: Equatable {
    let line: Line
    let kind: Kind
    let stopID: String   // the group's platform or bus stop, which ★ saves
}
