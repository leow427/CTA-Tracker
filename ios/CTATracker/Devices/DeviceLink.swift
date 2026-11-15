import CTAKit
import Foundation
import Network
import Observation
import os

/// Finds the boards on the Wi-Fi over Bonjour and talks to them over HTTP (ecosystem spec 4.2, 4.4): polls each one's
/// `GET /info` every 2 s while the app is active and feeds every answer, or miss, to its `DeviceWatch`.
/// The wearable goes over Bluetooth first (`BLELink`): its notifies replace the poll, and writes fall back to HTTP.
@Observable final class DeviceLink {
    struct Device: Identifiable, Codable, Equatable {
        let id: String; let kind: DeviceKind; var name: String; var lastIP: String?
        var peripheral: UUID? = nil   // the wearable's Bluetooth identity on this phone
    }
    enum LinkError: Error, Equatable { case noAddress, noToken, status(Int), disconnected }
    /// What `POST /pair` came to (ecosystem spec 4.3; Leo 2026-11-14: 423 is a 30 s cooldown, then a new code).
    nonisolated enum PairResult: Equatable { case paired, wrongCode, cooldown, taken, unreachable }
    enum Transport {
        case bluetooth, wifi
        var label: String { self == .bluetooth ? "via Bluetooth" : "via Wi-Fi" }
    }

    static let pollSec = 2.0   // ecosystem spec 4.2

    /// Devices set up from this app, saved with the address each was last found at.
    var devices: [Device] { didSet { KnownDevices.save(devices); ble.known = Self.peripherals(devices) } }
    let ble: BLELink
    private(set) var online: [String: InfoMessage] = [:]   // by device id
    /// What each wearable was last told it shows, for the relay's stop set (plan AB3).
    @ObservationIgnored private(set) var sentNearby: [String: NearbyMessage] = [:]
    @ObservationIgnored private(set) var sentPin: [String: ConfigStop] = [:]
    @ObservationIgnored var onSent: (() -> Void)?   // the relay sends a changed stop set at once
    var onAction: ((String, DeviceWatch.Action) -> Void)?   // AppModel reacts (Tasks 13–17)
    @ObservationIgnored var onAdded: ((String) -> Void)?   // a device just paired: AppModel drops the old wrist pin

    @ObservationIgnored private var found: [String: String] = [:]   // id -> "ip:port" Bonjour gave this session
    @ObservationIgnored private var resolving: Set<String> = []
    @ObservationIgnored private var watches: [String: DeviceWatch] = [:]
    @ObservationIgnored private var browser: NWBrowser?
    @ObservationIgnored private var poll: Task<Void, Never>?
    private let session: URLSession = {
        let c = URLSessionConfiguration.ephemeral
        c.timeoutIntervalForRequest = 1.5   // under the poll interval, so polls never pile up
        return URLSession(configuration: c)
    }()
    private let log = Logger(subsystem: "io.github.leow427.CTATracker", category: "link")

    init() {
        let saved = KnownDevices.load()
        devices = saved
        ble = BLELink(known: Self.peripherals(saved))
        ble.onInfo = { [weak self] id, info in self?.feed(id, info) }
    }

    /// How the app reaches this device right now, for the device page; nil while it's offline.
    func transport(of id: String) -> Transport? {
        ble.connected[id] != nil ? .bluetooth : online[id] != nil ? .wifi : nil
    }

    /// Browses and polls while the app is active; `false` stops both (nothing runs in the background).
    func setActive(_ active: Bool) {
        poll?.cancel()
        poll = nil
        browser?.cancel()
        browser = nil
        found = [:]   // re-resolve on each activation: a board may have moved while we were away
        ble.setActive(active)
        guard active else { return }
        browse()
        poll = Task {
            while !Task.isCancelled {
                let next = ContinuousClock.now + .seconds(Self.pollSec)
                // A wearable on Bluetooth reports by notify; its poll resumes the moment the link drops.
                for id in Set(devices.map(\.id)).union(found.keys).union(watches.keys) where ble.connected[id] == nil {
                    Task { feed(id, try? await get("/info", from: id)) }
                }
                try? await Task.sleep(until: next)
            }
        }
    }

    func get<T: Decodable>(_ path: String, from id: String) async throws -> T {
        try JSONDecoder().decode(T.self, from: await send(Self.request("GET", path, address: address(of: id), token: nil)))
    }

    func post(_ path: String, _ body: some Encodable, to id: String) async throws {
        try await write("POST", path, JSONEncoder().encode(body), to: id)
        if path == "/nearby", let n = body as? NearbyMessage { sentNearby[id] = n; onSent?() }
        if path == "/pin", let s = body as? ConfigStop { sentPin[id] = s; onSent?() }
        // Following pins the stop on the wearable too, so it gets relayed like a pin.
        if path == "/follow", let f = body as? FollowMessage { sentPin[id] = f.stop; onSent?() }
    }

    func delete(_ path: String, to id: String) async throws {
        try await write("DELETE", path, nil, to: id)
        if path == "/pin" { sentPin[id] = nil }
    }

    /// Boards answering on the network that have no token: new ones, and known ones that were reset.
    var unpaired: [InfoMessage] { online.values.filter { !$0.paired }.sorted { $0.id < $1.id } }

    /// Pairs with the code on the board's screen. A 409 may be our own earlier try whose 200 got lost, so it's
    /// confirmed with a write carrying `token` before it counts as taken.
    func pair(_ id: String, code: String, token: String) async -> PairResult {
        let address = address(of: id)
        guard var r = try? Self.request("POST", "/pair", address: address, token: token,
                                        body: try? JSONEncoder().encode(PairMessage(code: code, token: token))) else { return .unreachable }
        r.setValue(nil, forHTTPHeaderField: "Authorization")   // /pair takes no bearer (spec 4.3)
        let answer = await status(of: r)
        guard answer == 409 else { return Self.pairResult(answer) }
        // `{}` changes nothing; a 200 means the board holds our token.
        var confirmed: Int?
        if let confirm = try? Self.request("POST", "/config", address: address, token: token,
                                           body: try? JSONEncoder().encode(ConfigMessage())) {
            confirmed = await status(of: confirm)
        }
        return Self.pairResult(409, confirm: confirmed)
    }

    /// Saves a device just set up; one already known by that id is updated in place (re-pairing).
    func add(_ id: String, kind: DeviceKind, peripheral: UUID?) {
        devices = Self.adding(id, kind: kind, peripheral: peripheral, lastIP: found[id], to: devices)
        onAdded?(id)
    }

    /// Removes the device; a Bluetooth bond stays until the user forgets it in iOS Settings (app spec 6.3).
    func forget(_ id: String) {
        devices.removeAll { $0.id == id }
        watches[id] = nil
        online[id] = nil
    }

    // MARK: - Pure parts (tested)

    /// Bluetooth when it's connected, else HTTP; a Bluetooth write lost to a disconnect is retried once over HTTP.
    /// With no `http` (a Bluetooth-only path) the write fails instead.
    static func route(ble: (() async throws -> Void)?, http: (() async throws -> Void)?) async throws {
        guard let ble else {
            guard let http else { throw LinkError.disconnected }
            return try await http()
        }
        do { try await ble() } catch LinkError.disconnected where http != nil { try await http?() }
    }

    /// The Wi-Fi password and the household token go only over the bond, never in clear on the LAN (spec 4.3, 4.4).
    nonisolated static func httpAllowed(_ path: String) -> Bool { path != "/wifi" && path != "/token" }

    nonisolated static func peripherals(_ devices: [Device]) -> Set<UUID> { Set(devices.compactMap(\.peripheral)) }

    /// nil (a network error) is unreachable. A 409 counts as ours when the authenticated `confirm` write got 200.
    nonisolated static func pairResult(_ status: Int?, confirm: Int? = nil) -> PairResult {
        switch status {
        case 200: .paired
        case 403: .wrongCode
        case 423: .cooldown
        case 409: confirm == 200 ? .paired : confirm == nil ? .unreachable : .taken
        default: .unreachable
        }
    }

    nonisolated static func adding(_ id: String, kind: DeviceKind, peripheral: UUID?, lastIP: String?,
                                   to devices: [Device]) -> [Device] {
        var devices = devices
        if let i = devices.firstIndex(where: { $0.id == id }) {
            devices[i].peripheral = peripheral   // a code pairing means the board wiped its bond (Leo 2026-11-14)
            devices[i].lastIP = lastIP ?? devices[i].lastIP
        } else {
            devices.append(Device(id: id, kind: kind, name: kind == .dock ? "Dock" : "Trackie", lastIP: lastIP,
                                  peripheral: peripheral))
        }
        return devices
    }

    /// Bonjour's answer this session, else the address the device last had: blocked mDNS alone isn't fatal (spec 7).
    nonisolated static func address(of id: String, found: [String: String], known: [Device]) -> String? {
        found[id] ?? known.first { $0.id == id }?.lastIP
    }

    /// Writes carry the bearer token and fail without one; reads never carry it (spec 4.3).
    nonisolated static func request(_ method: String, _ path: String, address: String?, token: String?,
                                    body: Data? = nil) throws -> URLRequest {
        guard let address, let url = URL(string: "http://\(address)\(path)") else { throw LinkError.noAddress }
        var r = URLRequest(url: url)
        r.httpMethod = method
        if method != "GET" {
            guard let token else { throw LinkError.noToken }
            r.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        }
        if let body {
            r.httpBody = body
            r.setValue("application/json", forHTTPHeaderField: "Content-Type")
        }
        return r
    }

    /// "ip:port" for a resolved IPv4 endpoint.
    nonisolated static func address(_ endpoint: NWEndpoint) -> String? {
        guard case let .hostPort(host, port) = endpoint, case let .ipv4(ip) = host else { return nil }
        return ip.rawValue.map(String.init).joined(separator: ".") + ":\(port.rawValue)"
    }

    // MARK: - Network

    private func address(of id: String) -> String? { Self.address(of: id, found: found, known: devices) }

    private func write(_ method: String, _ path: String, _ body: Data?, to id: String) async throws {
        try await Self.route(
            ble: ble.connected[id] == nil ? nil : { try await self.ble.send(method, path, body, to: id) },
            http: !Self.httpAllowed(path) ? nil : {
                _ = try await self.send(Self.request(method, path, address: self.address(of: id),
                                                     token: TokenStore.token(), body: body))
            })
    }

    private func status(of request: URLRequest) async -> Int? {
        guard let (_, response) = try? await session.data(for: request) else { return nil }
        return (response as? HTTPURLResponse)?.statusCode
    }

    private func send(_ request: URLRequest) async throws -> Data {
        let (data, response) = try await session.data(for: request)
        let status = (response as? HTTPURLResponse)?.statusCode ?? 0
        guard status == 200 else { throw LinkError.status(status) }
        return data
    }

    private func feed(_ id: String, _ answer: InfoMessage?) {
        let info = answer?.id == id ? answer : nil   // another board now on that address is a miss for this one
        let actions = watches[id, default: DeviceWatch()].polled(info)
        if let info { online[id] = info }
        if actions.contains(.wentOffline) {
            online[id] = nil
            if !devices.contains(where: { $0.id == id }) { watches[id] = nil }   // a stranger's board: stop polling it
        }
        for action in actions {
            log.info("\(id, privacy: .public) \(String(describing: action), privacy: .public)")
            onAction?(id, action)
        }
    }

    private func browse() {
        let b = NWBrowser(for: .bonjourWithTXTRecord(type: "_ctatracker._tcp", domain: nil), using: .tcp)
        b.browseResultsChangedHandler = { [weak self] results, _ in
            MainActor.assumeIsolated { self?.update(results) }   // started on .main
        }
        b.start(queue: .main)
        browser = b
    }

    private func update(_ results: Set<NWBrowser.Result>) {
        var endpoints: [String: NWEndpoint] = [:]
        for r in results {
            if case let .bonjour(txt) = r.metadata, let id = txt.dictionary["id"] { endpoints[id] = r.endpoint }
        }
        found = found.filter { endpoints[$0.key] != nil }
        // shortcut: an address is resolved once per Bonjour appearance; a board that changes IP while still listed keeps
        // the old one until it re-announces. Re-resolve on misses if DHCP churn shows up.
        for (id, endpoint) in endpoints where found[id] == nil && !resolving.contains(id) { resolve(id, endpoint) }
    }

    /// Connects once to learn the service's IPv4 address and port, then hangs up.
    private func resolve(_ id: String, _ endpoint: NWEndpoint) {
        resolving.insert(id)
        let params = NWParameters.tcp
        (params.defaultProtocolStack.internetProtocol as? NWProtocolIP.Options)?.version = .v4
        let c = NWConnection(to: endpoint, using: params)
        c.stateUpdateHandler = { [weak self] state in
            MainActor.assumeIsolated {
                switch state {
                case .ready:
                    if let a = c.currentPath?.remoteEndpoint.flatMap(Self.address) { self?.resolved(id, a) }
                    c.cancel()
                case .waiting, .failed:
                    c.cancel()
                case .cancelled:
                    self?.resolving.remove(id)
                default:
                    break
                }
            }
        }
        c.start(queue: .main)
    }

    private func resolved(_ id: String, _ address: String) {
        found[id] = address
        if let i = devices.firstIndex(where: { $0.id == id }), devices[i].lastIP != address { devices[i].lastIP = address }
    }
}
