import CTAKit
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

    @Test func emptyDockSlotsStayInPlace() throws {
        let json = #"{"stops":[{},{"type":"train","route":"Red","dir":"1","id":"30173","name":"Howard","rtnm":""},{}],"pet":{}}"#
        let config = try JSONDecoder().decode(DockConfig.self, from: Data(json.utf8))
        #expect(config.stops?.map(\.stop?.id) == [nil, "30173", nil])
    }

    /// One section replaced, the others sent back as the dock gave them (a bus's `rtnm` too), an empty one as `{}`.
    @Test func dockBodyReplacesOneSection() throws {
        let json = #"{"stops":[{"type":"bus","route":"22","dir":"Southbound","id":"1828","name":"Clark & Belmont","rtnm":"Clark"},{"type":"train","route":"Red","dir":"Howard","id":"30255","name":"Belmont","rtnm":""},{}]}"#
        let current = try #require(try JSONDecoder().decode(DockConfig.self, from: Data(json.utf8)).stops)
        let brown = ConfigStop(dock: Line(route: "Brn", dir: "Loop"), kind: .station, stopID: "30258", name: "Belmont")
        let enc = JSONEncoder()
        enc.outputFormatting = .sortedKeys
        let body = String(decoding: try enc.encode(DockConfig(stops: DockConfig.replacing(current, section: 1, with: brown))), as: UTF8.self)
        #expect(body == #"{"stops":[{"dir":"Southbound","id":"1828","name":"Clark & Belmont","route":"22","rtnm":"Clark","type":"bus"},"#
            + #"{"dir":"Loop","id":"30258","name":"Belmont","route":"Brn","type":"train"},{}]}"#)
        // A short list (never from the dock, but never indexed past) pads to 3 empty sections.
        #expect(DockConfig.replacing([], section: 2, with: nil).count == 3)
    }
}
