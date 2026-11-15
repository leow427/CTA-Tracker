import CoreBluetooth
import CTAKit
import Foundation
import Testing
@testable import CTATracker

// The Bluetooth link's decisions, without a radio (wearable BLE link plan, Task AB1).
struct BLELinkTests {
    let infoJSON = Data("""
        {"type":"handheld","id":"c3d4","fw":"1.2.0","battery":81,"charging":false,
         "wifi":{"connected":false,"ssid":"","rssi":0},"last_update":1763100000,"uptime":42,
         "picks_ver":0,"wake":3,"docked":false,"paired":true,"relaying":false}
        """.utf8)

    // MARK: Routing

    @Test func bluetoothWhenConnectedHTTPOtherwise() async throws {
        var calls: [String] = []
        try await DeviceLink.route(ble: { calls.append("ble") }, http: { calls.append("http") })
        try await DeviceLink.route(ble: nil, http: { calls.append("http") })
        #expect(calls == ["ble", "http"])
    }

    @Test func disconnectRetriesOnceOverHTTP() async throws {
        var calls: [String] = []
        try await DeviceLink.route(ble: { calls.append("ble"); throw DeviceLink.LinkError.disconnected },
                                   http: { calls.append("http") })
        #expect(calls == ["ble", "http"])
    }

    @Test func wearableRefusalIsNotRetried() async {
        var calls: [String] = []
        await #expect(throws: DeviceLink.LinkError.status(400)) {
            try await DeviceLink.route(ble: { calls.append("ble"); throw DeviceLink.LinkError.status(400) },
                                       http: { calls.append("http") })
        }
        #expect(calls == ["ble"])   // the board answered: HTTP would get the same answer
    }

    @Test func wifiPasswordAndTokenNeverGoOverHTTP() async {
        #expect(!DeviceLink.httpAllowed("/wifi") && !DeviceLink.httpAllowed("/token"))
        #expect(DeviceLink.httpAllowed("/pin") && DeviceLink.httpAllowed("/config"))
        var calls: [String] = []
        await #expect(throws: DeviceLink.LinkError.disconnected) {
            try await DeviceLink.route(ble: { calls.append("ble"); throw DeviceLink.LinkError.disconnected }, http: nil)
        }
        await #expect(throws: DeviceLink.LinkError.disconnected) { try await DeviceLink.route(ble: nil, http: nil) }
        #expect(calls == ["ble"])
    }

    @Test func transportLabel() {
        #expect(DeviceLink.Transport.bluetooth.label == "via Bluetooth")
        #expect(DeviceLink.Transport.wifi.label == "via Wi-Fi")
    }

    // MARK: Errors

    @Test(arguments: [(0x80, 400), (0x81, 413), (0x82, 404), (0x83, 0),
                      (CBATTError.insufficientAuthentication.rawValue, 401),
                      (CBATTError.insufficientEncryption.rawValue, 401)])
    func attErrorsMapToHTTPStatus(code: Int, status: Int) {
        let e = NSError(domain: CBATTErrorDomain, code: code)
        #expect(BLELink.linkError(e) == .status(status))
    }

    @Test func linkLossIsADisconnect() {
        #expect(BLELink.linkError(NSError(domain: CBErrorDomain, code: CBError.peripheralDisconnected.rawValue)) == .disconnected)
        #expect(BLELink.linkError(NSError(domain: CBErrorDomain, code: CBError.connectionTimeout.rawValue)) == .disconnected)
    }

    // MARK: Chunking

    @Test func writesAreFramedEnvelopesWithinTheWriteLength() throws {
        let body = Data(#"{"type":"bus","route":"22","dir":"Northbound","id":"1842","name":"Clark & Belmont"}"#.utf8)
        let chunks = try #require(BLELink.chunks("POST", "/pin", body, maxWrite: 20))
        #expect(chunks.count > 3)
        #expect(chunks.allSatisfy { $0.count <= 20 })
        var r = BLEFrames.Reassembler()
        let whole = chunks.compactMap { r.add($0) }
        #expect(whole == [Data("POST /pin\n".utf8) + body])
    }

    @Test func oversizeWriteIsRefused() {
        #expect(BLELink.chunks("POST", "/stop", Data(count: BLEFrames.maxMessage), maxWrite: 182) == nil)
    }

    // MARK: Notifies

    @Test func infoReassembledAcrossThreeChunks() throws {
        let chunkMax = infoJSON.count / 3 + 2
        let chunks = try #require(BLEFrames.split(infoJSON, chunkMax: chunkMax))
        #expect(chunks.count == 3)
        var r = BLEFrames.Reassembler()
        #expect(BLELink.info(chunks[0], &r) == nil)
        #expect(BLELink.info(chunks[1], &r) == nil)
        let info = try #require(BLELink.info(chunks[2], &r))
        #expect(info.id == "c3d4" && info.battery == 81 && info.paired)
    }

    @Test func brokenNotifyIsDropped() throws {
        let chunks = try #require(BLEFrames.split(infoJSON, chunkMax: infoJSON.count / 3 + 2))
        var r = BLEFrames.Reassembler()
        #expect(BLELink.info(chunks[0], &r) == nil)
        #expect(BLELink.info(chunks[2], &r) == nil)   // gap: wait for the next notify
        #expect(chunks.compactMap { BLELink.info($0, &r) }.count == 1)   // and the next whole one lands
    }

    // MARK: Reconnect

    @Test func disconnectDropsTheLinkAndReconnectsKnownWearables() {
        let known = UUID(), stranger = UUID()
        let connected = ["c3d4": known, "e5f6": stranger]
        let a = BLELink.afterDisconnect(known, connected: connected, known: [known])
        #expect(a.connected == ["e5f6": stranger] && a.reconnect)
        let b = BLELink.afterDisconnect(stranger, connected: connected, known: [known])
        #expect(b.connected == ["c3d4": known] && !b.reconnect)
        let bond = CBError(.peerRemovedPairingInformation)
        let broken = BLELink.afterDisconnect(known, connected: connected, known: [known], error: bond)
        #expect(!broken.reconnect && broken.addAgain)   // the wearable was reset: "Add again"
        #expect(!a.addAgain && !b.addAgain)
        #expect(BLELink.afterDisconnect(known, connected: connected, known: [known], error: CBError(.connectionTimeout)).reconnect)
    }

    @Test func knownPeripheralsComeFromSavedDevices() {
        let p = UUID()
        let devices = [DeviceLink.Device(id: "a1b2", kind: .dock, name: "Dock", lastIP: nil),
                       DeviceLink.Device(id: "c3d4", kind: .handheld, name: "Trackie", lastIP: nil, peripheral: p)]
        #expect(DeviceLink.peripherals(devices) == [p])
    }
}
