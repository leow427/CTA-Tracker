import CoreBluetooth
import CTAKit
import Foundation
import Testing
@testable import CTATracker

// The setup flow's decisions, without a board (plan Task AB2; Leo's pairing rules of 2026-11-14).
struct SetupFlowTests {
    let p = UUID()

    func info(_ id: String, _ type: DeviceKind, paired: Bool = false) throws -> InfoMessage {
        try JSONDecoder().decode(InfoMessage.self, from: Data("""
            {"type":"\(type.rawValue)","id":"\(id)","fw":"1","battery":null,"charging":false,
             "wifi":{"connected":true,"ssid":"Home"},"last_update":0,"uptime":5,"picks_ver":0,"wake":0,
             "docked":false,"paired":\(paired)}
            """.utf8))
    }

    // MARK: Bluetooth or the code

    @Test func wearablePairsOverBluetoothWhenItCan() {
        let m = SetupFlow.method(kind: .handheld, flag: true, bluetoothOn: true, peripheral: p, deviceID: "c3d4", waited: false)
        #expect(m == .bluetooth(p))
    }

    @Test func wearableFallsBackToTheCode() {
        // Bluetooth off or not allowed
        #expect(SetupFlow.method(kind: .handheld, flag: true, bluetoothOn: false, peripheral: p, deviceID: "c3d4", waited: false) == .code("c3d4"))
        // the cut-line flag off
        #expect(SetupFlow.method(kind: .handheld, flag: false, bluetoothOn: true, peripheral: p, deviceID: "c3d4", waited: false) == .code("c3d4"))
        // not seen over Bluetooth within the wait
        #expect(SetupFlow.method(kind: .handheld, flag: true, bluetoothOn: true, peripheral: nil, deviceID: "c3d4", waited: false) == .waiting)
        #expect(SetupFlow.method(kind: .handheld, flag: true, bluetoothOn: true, peripheral: nil, deviceID: "c3d4", waited: true) == .code("c3d4"))
        // the radio hasn't reported yet: wait for it
        #expect(SetupFlow.method(kind: .handheld, flag: true, bluetoothOn: nil, peripheral: p, deviceID: nil, waited: false) == .waiting)
    }

    @Test func noWayToPair() {
        #expect(SetupFlow.method(kind: .handheld, flag: true, bluetoothOn: false, peripheral: p, deviceID: nil, waited: false) == .unavailable)
        #expect(SetupFlow.method(kind: .handheld, flag: true, bluetoothOn: true, peripheral: nil, deviceID: nil, waited: true) == .unavailable)
    }

    @Test func dockIsCodeOnly() {
        #expect(SetupFlow.method(kind: .dock, flag: true, bluetoothOn: true, peripheral: p, deviceID: "a1b2", waited: false) == .code("a1b2"))
    }

    @Test func radioStates() {
        #expect(SetupFlow.bluetoothOn(.poweredOn) == true)
        #expect(SetupFlow.bluetoothOn(.poweredOff) == false)
        #expect(SetupFlow.bluetoothOn(.unauthorized) == false)
        #expect(SetupFlow.bluetoothOn(.unsupported) == false)   // the simulator: straight to the code
        #expect(SetupFlow.bluetoothOn(nil) == nil)
        #expect(SetupFlow.bluetoothOn(.resetting) == nil)
    }

    // MARK: Steps

    @Test func stepsAfterPairing() {
        #expect(SetupFlow.next(after: .pair, viaBluetooth: true, firstDevice: true) == .wifi)
        #expect(SetupFlow.next(after: .pair, viaBluetooth: true, firstDevice: false) == .wifi)   // Add again
        #expect(SetupFlow.next(after: .pair, viaBluetooth: false, firstDevice: true) == .pet)   // code: on Wi-Fi already
        #expect(SetupFlow.next(after: .pair, viaBluetooth: false, firstDevice: false) == .ready)
        #expect(SetupFlow.next(after: .wifi, viaBluetooth: true, firstDevice: true) == .pet)
        #expect(SetupFlow.next(after: .wifi, viaBluetooth: true, firstDevice: false) == .ready)   // the pet exists
        #expect(SetupFlow.next(after: .pet, viaBluetooth: true, firstDevice: true) == .ready)
    }

    // MARK: Code pairing

    @Test func pairStatuses() {
        #expect(DeviceLink.pairResult(200) == .paired)
        #expect(DeviceLink.pairResult(403) == .wrongCode)
        #expect(DeviceLink.pairResult(423) == .cooldown)
        // 409: our own earlier try whose 200 got lost, or another phone's; the authenticated confirm decides.
        #expect(DeviceLink.pairResult(409, confirm: 200) == .paired)
        #expect(DeviceLink.pairResult(409, confirm: 401) == .taken)
        #expect(DeviceLink.pairResult(409, confirm: nil) == .unreachable)
        #expect(DeviceLink.pairResult(nil) == .unreachable)
        #expect(DeviceLink.pairResult(500) == .unreachable)
    }

    @Test func pairCopy() {
        #expect(SetupFlow.pairMessage(.paired, device: "Trackie") == nil)
        #expect(SetupFlow.pairMessage(.wrongCode, device: "Trackie") == "That code didn't match.")
        #expect(SetupFlow.pairMessage(.cooldown, device: "Trackie") == "Too many tries, a new code will appear on the device.")
    }

    @Test func pairNeverShowsTokenOrCode() throws {
        let m = PairMessage(code: "482913", token: String(repeating: "ab", count: 16))
        for text in [String(describing: m), String(reflecting: m), "\(Mirror(reflecting: m).children.map(\.value))"] {
            #expect(!text.contains("482913") && !text.contains("abab"))
        }
        let json = try #require(String(data: JSONEncoder().encode(m), encoding: .utf8))
        #expect(json.contains("\"code\":\"482913\"") && json.contains("\"token\""))
    }

    // MARK: Devices

    @Test func addingANewDeviceAndRepairingAKnownOne() {
        let added = DeviceLink.adding("c3d4", kind: .handheld, peripheral: p, lastIP: nil, to: [])
        #expect(added == [DeviceLink.Device(id: "c3d4", kind: .handheld, name: "Trackie", lastIP: nil, peripheral: p)])

        let q = UUID()
        let renamed = [DeviceLink.Device(id: "c3d4", kind: .handheld, name: "Wrist", lastIP: "10.0.0.5:80", peripheral: p)]
        let again = DeviceLink.adding("c3d4", kind: .handheld, peripheral: q, lastIP: nil, to: renamed)
        #expect(again == [DeviceLink.Device(id: "c3d4", kind: .handheld, name: "Wrist", lastIP: "10.0.0.5:80", peripheral: q)])

        let byCode = DeviceLink.adding("c3d4", kind: .handheld, peripheral: nil, lastIP: "10.0.0.9:80", to: again)
        // A board takes a code only once it wiped its bond, so the old peripheral goes (no "Add again" loop).
        #expect(byCode.first?.peripheral == nil && byCode.first?.lastIP == "10.0.0.9:80")
    }

    // MARK: Found cards

    @Test func oneCardPerBoardWithItsAdvertisingMerged() throws {
        let stranger = UUID()
        let cards = SetupFlow.candidates(bleFound: [p: "Trackie-c3d4", stranger: "Trackie-9e01"],
                                         unpaired: [try info("a1b2", .dock), try info("c3d4", .handheld)],
                                         known: [], dismissed: [])
        #expect(cards.count == 3)
        #expect(cards[0].kind == .dock && cards[0].peripheral == nil && cards[0].title == "Dock found")
        #expect(cards[1].deviceID == "c3d4" && cards[1].peripheral == p && cards[1].name == "Trackie-c3d4")
        #expect(cards[2].deviceID == nil && cards[2].peripheral == stranger && cards[2].action == "Set up")
    }

    @Test func aResetKnownDeviceOffersAddAgain() throws {
        let known = [DeviceLink.Device(id: "a1b2", kind: .dock, name: "Dock", lastIP: nil),
                     DeviceLink.Device(id: "c3d4", kind: .handheld, name: "Trackie", lastIP: nil, peripheral: p)]
        let cards = SetupFlow.candidates(bleFound: [p: "Trackie-c3d4"], unpaired: [try info("a1b2", .dock)],
                                         known: known, dismissed: [])
        #expect(cards.map(\.again) == [true, true])   // unpaired on the network; bond lost over Bluetooth
        #expect(cards[0].title == "Dock was reset" && cards[0].action == "Add again")
    }

    @Test func aCodePairedWearableIsNotNewOverBluetooth() {
        // Set up with the code (no bond), it still advertises: no card.
        let byCode = [DeviceLink.Device(id: "c3d4", kind: .handheld, name: "Trackie", lastIP: "10.0.0.9:80")]
        #expect(SetupFlow.candidates(bleFound: [p: "Trackie-c3d4"], unpaired: [], known: byCode, dismissed: []).isEmpty)
    }

    @Test func aResetWearableUnderANewAddressIsAddAgain() {
        let bonded = [DeviceLink.Device(id: "c3d4", kind: .handheld, name: "Trackie", lastIP: nil, peripheral: UUID())]
        let cards = SetupFlow.candidates(bleFound: [p: "Trackie-c3d4"], unpaired: [], known: bonded, dismissed: [])
        #expect(cards.map(\.again) == [true] && cards.first?.title == "Trackie was reset")
    }

    @Test func aLostReplyKeepsTheCode() {
        #expect(SetupFlow.keepsCode(.unreachable))   // resend the same code and token; the 409 confirms
        #expect(!SetupFlow.keepsCode(.wrongCode) && !SetupFlow.keepsCode(.cooldown) && !SetupFlow.keepsCode(.taken))
    }

    @Test func notNowHidesACard() throws {
        let cards = SetupFlow.candidates(bleFound: [p: "Trackie-c3d4"], unpaired: [try info("a1b2", .dock)],
                                         known: [], dismissed: ["a1b2"])
        #expect(cards.map(\.id) == [p.uuidString])
    }

    @Test func advertisingMatchesTheBoardID() {
        #expect(SetupFlow.peripheral(for: "c3d4", in: [p: "Trackie-c3d4"]) == p)
        #expect(SetupFlow.peripheral(for: "c3d5", in: [p: "Trackie-c3d4"]) == nil)
        // Boards flashed before the rename still advertise the old name.
        #expect(SetupFlow.peripheral(for: "c3d4", in: [p: "CTA-Pet-C3D4"]) == p)
        #expect(SetupFlow.isTrackie("CTA-Pet-c3d4") && SetupFlow.isTrackie("Trackie-c3d4") && !SetupFlow.isTrackie("CTA-Dock-c3d4"))
    }
}
