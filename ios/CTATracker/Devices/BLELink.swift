import CoreBluetooth
import CTAKit
import Foundation
import Observation
import os

/// The wearable's Bluetooth link (wearable BLE link plan, "The link"): a central that keeps a pending connect to each
/// known wearable, takes its status from `info` notifies, and writes framed envelopes to `link`, one message at a time.
@Observable final class BLELink: NSObject {
    static let restoreID = "io.github.leow427.CTATracker.ble"
    static let service = CBUUID(string: BLEID.service)
    static let infoUUID = CBUUID(string: BLEID.info)
    static let linkUUID = CBUUID(string: BLEID.link)

    /// Device id -> peripheral, once an `info` notify came through (the characteristic is encrypted, so the link is).
    private(set) var connected: [String: UUID] = [:]
    /// Unknown `CTA-Pet-*` boards in range while scanning -> advertised name (AB2's found card).
    private(set) var found: [UUID: String] = [:]
    @ObservationIgnored var onInfo: ((String, InfoMessage) -> Void)?   // DeviceLink.feed

    /// Wearables set up from this app: always kept on a pending connect, foreground or not.
    @ObservationIgnored var known: Set<UUID> {
        didSet {
            // A forgotten wearable is let go; `drop` won't reconnect it.
            oldValue.subtracting(known).compactMap { peripherals[$0] }.forEach { central?.cancelPeripheralConnection($0) }
            connectKnown()
        }
    }

    @ObservationIgnored private var central: CBCentralManager?
    @ObservationIgnored private var active = false
    @ObservationIgnored private var peripherals: [UUID: CBPeripheral] = [:]   // CoreBluetooth needs them retained
    @ObservationIgnored private var links: [UUID: CBCharacteristic] = [:]
    @ObservationIgnored private var inboxes: [UUID: BLEFrames.Reassembler] = [:]
    @ObservationIgnored private var pendingWrite: [UUID: CheckedContinuation<Void, Error>] = [:]
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
              let p = peripherals[id] ?? central.retrievePeripherals(withIdentifiers: [id]).first else { return }
        peripherals[id] = p
        if p.state == .disconnected { central.connect(p) }
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
    /// not a tight loop that drains both batteries until the user forgets it in Settings.
    nonisolated static func afterDisconnect(_ peripheral: UUID, connected: [String: UUID], known: Set<UUID>,
                                            error: Error? = nil) -> (connected: [String: UUID], reconnect: Bool) {
        let bondBroken = (error as? CBError)?.code == .peerRemovedPairingInformation
        return (connected.filter { $0.value != peripheral }, known.contains(peripheral) && !bondBroken)
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

    private func connectKnown() {
        known.forEach(connect)
    }

    private func updateScan() {
        guard let central, central.state == .poweredOn else { return }
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
        if after.reconnect && reconnect { central?.connect(p) }   // pending: iOS completes it whenever the wearable comes back
    }
}

extension BLELink: @preconcurrency CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        log.info("state \(central.state.rawValue)")
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
        if known.contains(peripheral.identifier) {
            connect(peripheral.identifier)
        } else if name.hasPrefix("CTA-Pet-") {
            peripherals[peripheral.identifier] = peripheral
            found[peripheral.identifier] = name
        }
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        found[peripheral.identifier] = nil
        adopt(peripheral)
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        drop(peripheral, reconnect: false)
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        log.info("disconnected \(peripheral.identifier, privacy: .public)")
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
            if c.uuid == Self.infoUUID { peripheral.setNotifyValue(true, for: c) }
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
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
