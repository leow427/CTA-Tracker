import Foundation
import Testing
@testable import CTATracker

// The device row's lines (app spec 6.3).
struct DeviceListTests {
    @Test func statusSaysHowItIsReached() {
        #expect(DeviceList.status(online: false, transport: nil) == "Not connected")
        #expect(DeviceList.status(online: true, transport: .wifi) == "Online via Wi-Fi")
        #expect(DeviceList.status(online: true, transport: .bluetooth) == "Online via Bluetooth")
    }

    @Test func batteryOnlyWhenThereIsOne() {
        #expect(DeviceList.battery(nil, charging: false) == nil)   // the dock
        #expect(DeviceList.battery(62, charging: false) == "62%")
        #expect(DeviceList.battery(62, charging: true) == "62%, charging")
    }

    @Test func emptyDockSlotsAreSkipped() throws {
        let json = #"{"stops":[{"type":"train","route":"Red","dir":"1","id":"30173","name":"Howard","rtnm":"Red"},{},{}],"pet":{}}"#
        let config = try JSONDecoder().decode(DockConfig.self, from: Data(json.utf8))
        #expect(config.picked.map(\.id) == ["30173"])
        #expect(try JSONDecoder().decode(DockConfig.self, from: Data(#"{"stops":[{},{},{}]}"#.utf8)).picked.isEmpty)
    }
}
