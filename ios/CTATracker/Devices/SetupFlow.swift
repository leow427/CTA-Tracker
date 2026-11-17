import CoreBluetooth
import CTAKit
import Foundation
import Observation

/// `POST /pair`: the code on the board's screen and the household token. Its descriptions leave both out.
struct PairMessage: Encodable, CustomStringConvertible, CustomDebugStringConvertible, CustomReflectable {
    let code: String
    let token: String
    var description: String { "PairMessage(code: <redacted>, token: <redacted>)" }
    var debugDescription: String { description }
    var customMirror: Mirror { Mirror(self, children: [:]) }
}

/// A board the app could set up: seen over Bluetooth, answering unpaired on the network, or both.
nonisolated struct SetupCandidate: Identifiable, Equatable {
    let kind: DeviceKind
    var peripheral: UUID? = nil     // advertising `Trackie-*`
    var name: String? = nil         // its advertised name, for the iOS Settings hint
    var deviceID: String? = nil     // on the network with `paired: false`
    var again = false               // already in the app's list: it was reset (Leo 2026-11-14)
    var id: String { deviceID ?? peripheral?.uuidString ?? "" }

    var title: String { (kind == .dock ? "Dock" : "Trackie") + (again ? " was reset" : " found") }
    var action: String { again ? "Add again" : "Set up" }
}

/// The setup flow (app spec 6.2, ecosystem spec 5.1, plan Task AB2): Pair over Bluetooth or with the code on the
/// board, then Wi-Fi (wearable over Bluetooth only, optional), Pet (first device only), Ready.
@Observable final class SetupFlow {
    enum Step: Equatable { case pair, wifi, pet, ready }
    /// How Pair goes: the passkey in iOS's prompt, the code typed in the app, still looking, or no way at all.
    enum Method: Equatable { case bluetooth(UUID), code(String), waiting, unavailable }

    static let lookSec = 5.0           // a Bonjour-found wearable gets this long to show up over Bluetooth
    static let bondSec = 30.0          // the passkey prompt and the first notify
    static let wifiSec = 20.0          // app spec 6.2
    static let maxNetworks = 5         // kWifiSlots

    let candidate: SetupCandidate
    let firstDevice: Bool
    /// "Add a network" on a set-up wearable's page: only the Wi-Fi step, into the slots after the ones it has.
    private(set) var addingNetwork = false
    private(set) var firstSlot = 0
    private(set) var step: Step = .pair
    private(set) var method: Method = .waiting
    private(set) var busy = false
    private(set) var message: String?          // the step's one line of trouble
    private(set) var bluetoothFailed = false   // offers the code instead, and the iOS Settings hint
    private(set) var asking = false            // the app's Bluetooth line, before iOS asks (app spec 3)
    private(set) var networks: [String] = []   // joined this setup, slot order
    var code = ""
    var ssid = ""
    var password = ""   // cleared once sent; never stored or logged
    var petName = "Trackie"   // SetupHost starts it at the app's pet name
    /// The Pet step names the one Trackie (`AppModel.pet`), which writes it to every device, this one included.
    @ObservationIgnored var onPetName: ((String) -> Void)?

    @ObservationIgnored private let link: DeviceLink
    @ObservationIgnored private let token = TokenStore.token() ?? Token.random()   // one per flow, so a retry matches
    @ObservationIgnored private var deviceID: String?
    @ObservationIgnored private var closed = false   // Cancel: nothing may connect or pair after this
    @ObservationIgnored private var fallbackID: String?   // the wearable on the network, as found when Bluetooth failed

    init(_ candidate: SetupCandidate, link: DeviceLink) {
        self.candidate = candidate
        self.link = link
        firstDevice = link.devices.isEmpty
        if candidate.kind == .dock, let id = candidate.deviceID { method = .code(id) }
    }

    /// The setup flow's Wi-Fi step for a wearable that's already set up and connected over Bluetooth. With no count
    /// (set up before slots were counted) it starts at slot 0, so the first network added becomes home.
    init(addingNetworkTo device: DeviceLink.Device, link: DeviceLink) {
        candidate = SetupCandidate(kind: .handheld, peripheral: device.peripheral, deviceID: device.id)
        self.link = link
        firstDevice = false
        deviceID = device.id
        method = .code(device.id)   // anything but `.waiting`, so `start` does nothing
        step = .wifi
        addingNetwork = true
        firstSlot = device.wifiSlots ?? 0
    }

    /// Another network fits in the wearable's slots.
    var canAddNetwork: Bool { firstSlot + networks.count < Self.maxNetworks }

    var viaBluetooth: Bool { if case .bluetooth = method { true } else { false } }
    var deviceName: String { candidate.kind == .dock ? "your dock" : "Trackie" }

    // MARK: - Pure parts (tested)

    /// Bluetooth for a wearable whenever it can work, the code otherwise (plan AB2, decision 4); the dock is code only.
    /// `bluetoothOn` nil means the radio hasn't reported yet.
    nonisolated static func method(kind: DeviceKind, flag: Bool, bluetoothOn: Bool?, peripheral: UUID?,
                                   deviceID: String?, waited: Bool) -> Method {
        if kind == .handheld && flag {
            if bluetoothOn == true, let peripheral { return .bluetooth(peripheral) }
            if bluetoothOn != false && !waited { return .waiting }
        }
        return deviceID.map(Method.code) ?? .unavailable
    }

    nonisolated static func bluetoothOn(_ state: CBManagerState?) -> Bool? {
        switch state {
        case .poweredOn: true
        case .poweredOff, .unauthorized, .unsupported: false
        default: nil
        }
    }

    nonisolated static func next(after step: Step, viaBluetooth: Bool, firstDevice: Bool) -> Step {
        switch step {
        case .pair where viaBluetooth: .wifi   // a code pairing means it's on Wi-Fi already
        case .pair, .wifi: firstDevice ? .pet : .ready
        case .pet, .ready: .ready
        }
    }

    /// Wearables advertise `Trackie-xxxx`; boards flashed before the rename still say `CTA-Pet-xxxx`.
    nonisolated static let advertPrefixes = ["trackie-", "cta-pet-"]

    nonisolated static func isTrackie(_ name: String) -> Bool {
        advertPrefixes.contains { name.lowercased().hasPrefix($0) }
    }

    /// Whether `name` is the advertising of the board whose id is `xxxx`.
    nonisolated static func advertises(_ name: String, id: String) -> Bool {
        advertPrefixes.contains { name.lowercased() == $0 + id.lowercased() }
    }

    /// The `Trackie-xxxx` advertising for the board whose id is `xxxx`.
    nonisolated static func peripheral(for id: String, in found: [UUID: String]) -> UUID? {
        found.first { advertises($0.value, id: id) }?.key
    }

    /// One card per board: network boards (merged with their advertising), then wearables seen only over Bluetooth.
    nonisolated static func candidates(bleFound: [UUID: String], unpaired: [InfoMessage], known: [DeviceLink.Device],
                                       dismissed: Set<String>) -> [SetupCandidate] {
        var ble = bleFound
        var out: [SetupCandidate] = unpaired.map { info in
            let p = info.type == .handheld ? peripheral(for: info.id, in: ble) : nil
            if let p { ble[p] = nil }
            return SetupCandidate(kind: info.type, peripheral: p, name: p.flatMap { bleFound[$0] }, deviceID: info.id,
                                  again: known.contains { $0.id == info.id })
        }
        for (p, name) in ble.sorted(by: { $0.value < $1.value }) {
            // The advert names the board: one set up with the code (no bond) isn't new, and one with an older bond
            // was reset and came back under a new address.
            let byName = known.first { advertises(name, id: $0.id) }
            if let byName, byName.peripheral == nil { continue }
            let again = known.contains { $0.peripheral == p } || byName != nil
            out.append(SetupCandidate(kind: .handheld, peripheral: p, name: name, again: again))
        }
        return out.filter { !dismissed.contains($0.id) }
    }

    /// Unreachable keeps the code: the board may have taken it, and resending it gets the 409 that confirms.
    nonisolated static func keepsCode(_ r: DeviceLink.PairResult) -> Bool { r == .unreachable }

    nonisolated static func pairMessage(_ r: DeviceLink.PairResult, device: String) -> String? {
        switch r {
        case .paired: nil
        case .wrongCode: "That code didn't match."
        case .cooldown: "Too many tries, a new code will appear on the device."
        case .taken: "Already set up. Start pairing again from \(device)'s Settings."
        case .unreachable: "Couldn't reach \(device). Check it's on the same Wi-Fi."
        }
    }

    // MARK: - Steps

    /// Picks how to pair, giving a Bonjour-found wearable a few seconds to appear over Bluetooth. A wearable on a
    /// fresh install first shows the app's Bluetooth line; `allowBluetooth` carries on from there.
    func start(asked: Bool = false) async {
        guard method == .waiting, !asking else { return }
        if candidate.kind == .handheld && BLELink.enabled {
            if link.ble.needsAsking && !asked { asking = true; return }
            link.ble.start()
            // The radio reports once iOS's own alert is answered; the 5 s look starts after that.
            while link.ble.state == nil && !Task.isCancelled { try? await Task.sleep(for: .milliseconds(250)) }
        }
        let deadline = ContinuousClock.now + .seconds(Self.lookSec)
        repeat {
            method = choose(waited: false)
            if method != .waiting { break }
            try? await Task.sleep(for: .milliseconds(250))
        } while ContinuousClock.now < deadline && !Task.isCancelled
        if method == .waiting { method = choose(waited: true) }
        guard !Task.isCancelled, !closed else { return }
        if case let .bluetooth(p) = method { await pairOverBluetooth(p) }
    }

    func allowBluetooth() async {
        asking = false
        await start(asked: true)
    }

    /// Leaving before Pair finished lets go of the new wearable's connection.
    func close() {
        closed = true
        if step == .pair, case let .bluetooth(p) = method { link.ble.cancel(p) }
    }

    /// Connects; subscribing to the encrypted `info` raises iOS's passkey prompt. Once a notify comes through,
    /// the household token goes over the bond.
    func pairOverBluetooth(_ p: UUID) async {
        guard !closed else { return }
        busy = true
        message = nil
        bluetoothFailed = false
        defer { busy = false }
        link.ble.connect(p)
        let deadline = ContinuousClock.now + .seconds(Self.bondSec)
        var id = link.ble.connected.first { $0.value == p }?.key
        while id == nil && !link.ble.failed.contains(p) && ContinuousClock.now < deadline && !Task.isCancelled && !closed {
            try? await Task.sleep(for: .milliseconds(250))
            id = link.ble.connected.first { $0.value == p }?.key
        }
        guard !closed else { link.ble.cancel(p); return }
        guard let id, (try? await link.post("/token", TokenMessage(token: token), to: id)) != nil, !closed else {
            link.ble.cancel(p)   // "Try again" connects afresh
            // The wearable may have reached the network since its card was tapped.
            let name = candidate.name ?? ""
            fallbackID = candidate.deviceID ?? link.unpaired.first { Self.peripheral(for: $0.id, in: [p: name]) != nil }?.id
            bluetoothFailed = true
            message = "Couldn't pair over Bluetooth."
            return
        }
        paired(id, peripheral: p)
    }

    /// The code fallback after a failed Bluetooth pairing, when the wearable is on the network unpaired.
    var codeFallback: String? { bluetoothFailed ? fallbackID : nil }
    func useCode() { if let id = codeFallback { method = .code(id); message = nil } }

    func submitCode() async {
        guard case let .code(id) = method, code.count == 6, !busy else { return }
        busy = true
        defer { busy = false }
        let result = await link.pair(id, code: code, token: token)
        message = Self.pairMessage(result, device: deviceName)
        if result == .paired { paired(id, peripheral: nil) } else if !Self.keepsCode(result) { code = "" }
    }

    func joinWifi() async {
        guard let id = deviceID, !ssid.isEmpty, canAddNetwork, !busy else { return }
        busy = true
        message = nil
        defer { busy = false }
        let name = ssid, wifi = WifiMessage(slot: firstSlot + networks.count, ssid: name, pass: password)
        password = ""
        // shortcut: success is "joined a network" (plan AB2); a later slot looks joined while the wearable stays on a
        // stronger one. Compare `info.wifi.ssid` if that misleads.
        let deadline = ContinuousClock.now + .seconds(Self.wifiSec)
        var joined = false
        if (try? await link.post("/wifi", wifi, to: id)) != nil {
            while !joined && ContinuousClock.now < deadline && !Task.isCancelled {
                try? await Task.sleep(for: .milliseconds(500))
                joined = link.online[id]?.wifi.connected == true
            }
        }
        if joined {
            networks.append(name)
            ssid = ""
            link.setWifiSlots(firstSlot + networks.count, of: id)
        } else { message = Self.wifiMessage(link.online[id]?.wifi.fail) }
    }

    /// The board's reason for a failed join (`info.wifi.fail`); an old board without it gets the generic line.
    nonisolated static func wifiMessage(_ fail: String?) -> String {
        switch fail {
        case "password": "Wrong password."
        case "no_network": "Couldn't find that network nearby."
        case "refused": "The network turned Trackie away. Some networks only let in devices they know: ask its admin to allow Trackie."
        default: "Couldn't join. Check the password."
        }
    }

    func finishWifi() { step = Self.next(after: .wifi, viaBluetooth: true, firstDevice: firstDevice) }

    func savePet() {
        // A failed write isn't fatal: the app writes the pet again on every connect (app spec 6.4).
        onPetName?(petName.trimmingCharacters(in: .whitespaces))
        step = .ready
    }

    // MARK: - Private

    private func choose(waited: Bool) -> Method {
        let ble = link.ble
        let p = candidate.peripheral ?? candidate.deviceID.flatMap { ble.connected[$0] ?? Self.peripheral(for: $0, in: ble.found) }
        return Self.method(kind: candidate.kind, flag: BLELink.enabled, bluetoothOn: Self.bluetoothOn(ble.state),
                           peripheral: p, deviceID: candidate.deviceID, waited: waited)
    }

    private func paired(_ id: String, peripheral: UUID?) {
        if TokenStore.token() == nil { TokenStore.setToken(token) }
        link.add(id, kind: candidate.kind, peripheral: peripheral)
        // The count starts over, so "Add a network" never skips slot 0 (home). Pair again keeps the board's Wi-Fi, so
        // its first network added then replaces the saved home; accepted, the app can't read the board's slots.
        if candidate.kind == .handheld { link.setWifiSlots(0, of: id) }
        deviceID = id
        message = nil
        step = Self.next(after: .pair, viaBluetooth: peripheral != nil, firstDevice: firstDevice)
    }

    #if DEBUG
    /// Screenshots: a flow parked on one step (`-setupDemo pair|asking|code|wifi|pet|ready|failed`), or the Wi-Fi
    /// step after a failed join (`wifi-password|wifi-no_network|wifi-refused|wifi-other`).
    init(demo: String, link: DeviceLink) {
        candidate = SetupCandidate(kind: .handheld, peripheral: UUID(), name: "Trackie-c3d4", deviceID: "c3d4")
        self.link = link
        firstDevice = true
        switch demo {
        case "code": method = .code("c3d4"); code = "48"
        case "failed":
            method = .bluetooth(UUID()); bluetoothFailed = true; fallbackID = "c3d4"
            message = "Couldn't pair over Bluetooth."
        case "wifi": method = .bluetooth(UUID()); step = .wifi; networks = ["Home"]; ssid = "Campus"
        case let d where d.hasPrefix("wifi-"):
            method = .bluetooth(UUID()); step = .wifi; networks = ["Home"]; ssid = "Campus"
            message = Self.wifiMessage(String(d.dropFirst(5)))
        case "asking": asking = true
        case "pet": step = .pet
        case "ready": step = .ready
        default: method = .bluetooth(UUID()); busy = true
        }
    }
    #endif
}
