import CoreBluetooth
import CTAKit
import Foundation
import Observation
import os

/// The wearable's Bluetooth link (wearable BLE link plan, "The link"): a central that keeps a pending connect to each
/// known wearable, takes its status from `info` notifies, and writes framed envelopes to `link`, one message at a time.
@Observable final class BLELink: NSObject {
    static let restoreID = "io.github.leow427.CTATracker.ble"
    /// The cut line (plan, 11/19): false sends every wearable setup to the code on its screen.
    static let enabled = true
    static let service = CBUUID(string: BLEID.service)
    static let infoUUID = CBUUID(string: BLEID.info)
    static let linkUUID = CBUUID(string: BLEID.link)

    /// Device id -> peripheral, once an `info` notify came through (the characteristic is encrypted, so the link is).
    private(set) var connected: [String: UUID] = [:]
    /// Unknown `Trackie-*` (or older `CTA-Pet-*`) boards in range while scanning, and known ones whose bond broke -> advertised name.
    private(set) var found: [UUID: String] = [:]
    /// Peripherals whose last connect or subscription failed, so setup can stop waiting; a new `connect` clears it.
    private(set) var failed: Set<UUID> = []
    /// The radio: nil until it reports, or before `start()`.
    private(set) var state: CBManagerState?
    /// Not started and never asked: the app's own Bluetooth line comes before iOS's prompt (app spec 3).
    var needsAsking: Bool { central == nil && CBManager.authorization == .notDetermined }
    @ObservationIgnored var onInfo: ((String, InfoMessage) -> Void)?   // DeviceLink.feed

    /// Wearables set up from this app: always kept on a pending connect, foreground or not.
    @ObservationIgnored var known: Set<UUID> {
        didSet {
            // A forgotten wearable is let go; `drop` won't reconnect it.
            oldValue.subtracting(known).compactMap { peripherals[$0] }.forEach { central?.cancelPeripheralConnection($0) }
            connectKnown()
        }
    }

    private var central: CBCentralManager?
    @ObservationIgnored private var active = false
    @ObservationIgnored private var peripherals: [UUID: CBPeripheral] = [:]   // CoreBluetooth needs them retained
    @ObservationIgnored private var links: [UUID: CBCharacteristic] = [:]
    @ObservationIgnored private var inboxes: [UUID: BLEFrames.Reassembler] = [:]
    @ObservationIgnored private var pendingWrite: [UUID: CheckedContinuation<Void, Error>] = [:]
    @ObservationIgnored private var pended: Set<UUID> = []   // pending connects made by this run
    @ObservationIgnored private var tail: Task<Void, Error>?   // the last queued message: writes go in order
    private let log = Logger(subsystem: "io.github.leow427.CTATracker", category: "ble")

    init(known: Set<UUID>) {
        self.known = known
        super.init()
        if CBManager.authorization != .notDetermined { start() }   // restoration needs the central at launch
    }

    /// Creates the central, which asks for Bluetooth the first time: AB2's setup calls this after its own explanation
    /// (app spec 3), so a fresh install with no wearable never sees the prompt.
    func start() {
        guard central == nil else { return }
        central = CBCentralManager(delegate: self, queue: .main,
                                   options: [CBCentralManagerOptionRestoreIdentifierKey: Self.restoreID])
    }

    /// Scans for new wearables while the app is active; pending connects to known ones run regardless.
    func setActive(_ active: Bool) {
        self.active = active
        updateScan()
    }

    /// Connects to a peripheral seen in a scan or saved with a device.
    func connect(_ id: UUID) {
        guard let central, central.state == .poweredOn,
              let p = peripherals[id] ?? central.retrievePeripherals(withIdentifiers: [id]).first else {
            log.info("connect \(id, privacy: .public): not retrievable")
            return
        }
        peripherals[id] = p
        failed.remove(id)
        let replace = Self.replacesConnect(p.state, pendedThisRun: pended.contains(id))
        log.info("connect \(id, privacy: .public): state \(p.state.rawValue)\(replace ? ", replacing" : "", privacy: .public)")
        if replace { central.cancelPeripheralConnection(p) }
        if p.state == .disconnected || replace { pend(p) }
    }

    /// Drops a setup's connection that never became a known wearable, so iOS's passkey prompt can't pop up later.
    func cancel(_ id: UUID) {
        guard !known.contains(id), let p = peripherals[id] else { return }
        central?.cancelPeripheralConnection(p)
    }

    /// One message to the wearable, framed and written with response; the ATT answer is the HTTP status.
    func send(_ method: String, _ path: String, _ body: Data?, to id: String) async throws {
        let previous = tail
        let job = Task {
            _ = await previous?.result
            try await write(method, path, body, to: id)
        }
        tail = job
        try await job.value
    }

    // MARK: - Pure parts (tested)

    /// A pending connect this run didn't make is cancelled and made again: iOS keeps the last run's pending connect,
    /// and that one never reached the wearable (seen 2026-11-15: "connecting" for minutes while scans saw it
    /// advertising; a fresh connect landed in 1.3 s). This run's own are left alone: one may be landing right now.
    nonisolated static func replacesConnect(_ state: CBPeripheralState, pendedThisRun: Bool) -> Bool {
        state == .connecting && !pendedThisRun
    }

    /// The framed envelope for one write, chunked at the peripheral's write length (MTU − 3). Nil when too large.
    nonisolated static func chunks(_ method: String, _ path: String, _ body: Data?, maxWrite: Int) -> [Data]? {
        BLEFrames.split(BLEFrames.envelope(method, path, body), chunkMax: maxWrite)
    }

    /// A status message when this notify was its last chunk; a broken or unreadable one is dropped (they repeat).
    nonisolated static func info(_ chunk: Data, _ inbox: inout BLEFrames.Reassembler) -> InfoMessage? {
        inbox.add(chunk).flatMap { try? JSONDecoder().decode(InfoMessage.self, from: $0) }
    }

    /// The wearable's ATT answers as HTTP statuses (plan, "Envelope"); anything else means the link went away.
    nonisolated static func linkError(_ error: Error) -> DeviceLink.LinkError {
        let e = error as NSError
        guard e.domain == CBATTErrorDomain else { return .disconnected }
        switch e.code {
        case 0x80: return .status(400)
        case 0x81: return .status(413)
        case 0x82: return .status(404)
        case CBATTError.insufficientAuthentication.rawValue, CBATTError.insufficientEncryption.rawValue,
             CBATTError.insufficientAuthorization.rawValue: return .status(401)
        default: return .status(0)   // 0x83, a broken chunk sequence, among others
        }
    }

    /// A known wearable gets a pending connect again, unless its bond broke: then the next foreground scan retries,
    /// not a tight loop that drains both batteries until the user forgets it in Settings, and the app offers
    /// "Add again" (Leo 2026-11-14).
    nonisolated static func afterDisconnect(_ peripheral: UUID, connected: [String: UUID], known: Set<UUID>,
                                            error: Error? = nil)
        -> (connected: [String: UUID], reconnect: Bool, addAgain: Bool) {
        let bondBroken = (error as? CBError)?.code == .peerRemovedPairingInformation
        let isKnown = known.contains(peripheral)
        return (connected.filter { $0.value != peripheral }, isKnown && !bondBroken, isKnown && bondBroken)
    }

    // MARK: - Radio

    private func write(_ method: String, _ path: String, _ body: Data?, to id: String) async throws {
        guard let uuid = connected[id], let p = peripherals[uuid], let link = links[uuid] else {
            throw DeviceLink.LinkError.disconnected
        }
        guard let chunks = Self.chunks(method, path, body, maxWrite: p.maximumWriteValueLength(for: .withoutResponse))
        else { throw DeviceLink.LinkError.status(413) }
        log.info("\(id, privacy: .public) \(method, privacy: .public) \(path, privacy: .public) \(chunks.count) chunks")
        for chunk in chunks {
            // The link may have dropped (or come back as a new one) while this task waited for the main actor.
            guard links[uuid] === link else { throw DeviceLink.LinkError.disconnected }
            try await withCheckedThrowingContinuation { c in
                pendingWrite[uuid] = c
                p.writeValue(chunk, for: link, type: .withResponse)
            }
        }
    }

    private func pend(_ p: CBPeripheral) {
        pended.insert(p.identifier)
        central?.connect(p)
    }

    private func connectKnown() {
        known.forEach(connect)
    }

    private func updateScan() {
        guard Self.enabled, let central, central.state == .poweredOn else { return }
        if active {
            found = [:]
            central.scanForPeripherals(withServices: [Self.service])
        } else {
            central.stopScan()
        }
    }

    private func adopt(_ p: CBPeripheral) {
        peripherals[p.identifier] = p
        p.delegate = self
        if p.state == .connected { p.discoverServices([Self.service]) }
    }

    /// `reconnect: false` after a failed connect: the next foreground scan retries.
    private func drop(_ p: CBPeripheral, error: Error? = nil, reconnect: Bool = true) {
        let after = Self.afterDisconnect(p.identifier, connected: connected, known: known, error: error)
        connected = after.connected
        links[p.identifier] = nil
        inboxes[p.identifier] = nil
        pendingWrite.removeValue(forKey: p.identifier)?.resume(throwing: DeviceLink.LinkError.disconnected)
        // Pending: iOS completes it whenever the wearable comes back.
        if after.reconnect && reconnect { pend(p) }
        if after.addAgain { found[p.identifier] = p.name ?? "Trackie" }
        // A setup connect that went nowhere, or a reset wearable's stale bond: setup stops waiting.
        // Not a local cancel (setup's own, a forget, a replaced connect): those report no error.
        if (!known.contains(p.identifier) && error != nil) || after.addAgain { failed.insert(p.identifier) }
    }
}

extension BLELink: @preconcurrency CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        log.info("state \(central.state.rawValue)")
        state = central.state
        guard central.state == .poweredOn else {
            connected = [:]   // the radio is off: every link is gone, HTTP takes over
            links = [:]
            inboxes = [:]
            pendingWrite.values.forEach { $0.resume(throwing: DeviceLink.LinkError.disconnected) }
            pendingWrite = [:]
            return
        }
        // Restored or still-connected links rediscover their characteristics; the rest get a pending connect.
        peripherals.values.filter { $0.state == .connected && links[$0.identifier] == nil }.forEach(adopt)
        connectKnown()
        updateScan()
    }

    /// iOS relaunched the app for a Bluetooth event: take the peripherals back before the radio reports in.
    func centralManager(_ central: CBCentralManager, willRestoreState dict: [String: Any]) {
        for p in dict[CBCentralManagerRestoredStatePeripheralsKey] as? [CBPeripheral] ?? [] {
            peripherals[p.identifier] = p
            p.delegate = self
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let name = advertisementData[CBAdvertisementDataLocalNameKey] as? String ?? peripheral.name ?? ""
        log.info("seen \(name, privacy: .public) \(peripheral.identifier, privacy: .public) known \(self.known.contains(peripheral.identifier))")
        if known.contains(peripheral.identifier) {
            connect(peripheral.identifier)
        } else if SetupFlow.isTrackie(name) {
            peripherals[peripheral.identifier] = peripheral
            found[peripheral.identifier] = name
        }
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        log.info("connected \(peripheral.identifier, privacy: .public)")
        found[peripheral.identifier] = nil
        adopt(peripheral)
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        log.info("failed to connect \(peripheral.identifier, privacy: .public): \(String(describing: error), privacy: .public)")
        drop(peripheral, error: error, reconnect: false)   // a broken bond often shows up here
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        log.info("disconnected \(peripheral.identifier, privacy: .public): \(String(describing: error), privacy: .public)")
        drop(peripheral, error: error)
    }
}

extension BLELink: @preconcurrency CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard let s = peripheral.services?.first(where: { $0.uuid == Self.service }) else { return }
        peripheral.discoverCharacteristics([Self.infoUUID, Self.linkUUID], for: s)
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        for c in service.characteristics ?? [] {
            if c.uuid == Self.linkUUID { links[peripheral.identifier] = c }
            // Subscribing to the encrypted `info` brings up iOS's pairing prompt on a new wearable.
            // The read too: NimBLE leaves the CCCD unprotected, but `info` itself needs encryption, so the read
            // makes iOS pair even when the subscribe alone wouldn't.
            if c.uuid == Self.infoUUID { peripheral.setNotifyValue(true, for: c); peripheral.readValue(for: c) }
        }
    }

    /// A refused `info` subscription: the passkey was cancelled or wrong, or the bond is stale.
    func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic,
                    error: Error?) {
        if error != nil { failed.insert(peripheral.identifier) }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        if error != nil && characteristic.uuid == Self.infoUUID { failed.insert(peripheral.identifier) }   // passkey refused
        guard characteristic.uuid == Self.infoUUID, error == nil, let chunk = characteristic.value,
              let info = Self.info(chunk, &inboxes[peripheral.identifier, default: BLEFrames.Reassembler()]) else { return }
        if connected[info.id] != peripheral.identifier { connected[info.id] = peripheral.identifier }
        onInfo?(info.id, info)
    }

    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        let c = pendingWrite.removeValue(forKey: peripheral.identifier)
        if let error { c?.resume(throwing: Self.linkError(error)) } else { c?.resume() }
    }
}
