import Foundation
import Testing
@testable import CTAKit

// Every BLE message the app writes must fit one 512-byte write (base spec 10).
struct BLEMessageTests {
    @Test func wifiWithLongestSSIDAndPassword() throws {
        let plain = WifiMessage(slot: 2, ssid: String(repeating: "S", count: 32), pass: String(repeating: "p", count: 63))
        #expect(try #require(BLEMessage.encode(plain)).count <= 512)
        // Worst case for JSON escaping: every character needs a backslash.
        let escaped = WifiMessage(slot: 2, ssid: String(repeating: "\"", count: 32), pass: String(repeating: "\\", count: 63))
        let data = try #require(BLEMessage.encode(escaped))
        #expect(data.count <= 512)
        let back = try #require(JSONSerialization.jsonObject(with: data) as? [String: Any])
        #expect(back["pass"] as? String == escaped.pass)
    }

    @Test func wifiPasswordNeverPrints() {
        let m = WifiMessage(slot: 0, ssid: "Home", pass: "hunter22secret")
        for text in [String(describing: m), String(reflecting: m), "\(m)", "\([m])"] {
            #expect(!text.contains("hunter22secret"), "\(text)")
            #expect(text.contains("Home"))
        }
        var dumped = ""
        dump(m, to: &dumped)
        #expect(!dumped.contains("hunter22secret"))
    }

    @Test func fullNearby() throws {
        let m = NearbyMessage(lat: 41.878119999999996, lon: -87.62979999999999,
                              trains: (0..<10).map { "4\($0)380" }, buses: (0..<10).map { "1\($0)999" },
                              close: ["41380", "10999", "11999"])
        let data = try #require(BLEMessage.encode(m))
        #expect(data.count <= 512)
        let json = String(decoding: data, as: UTF8.self)
        #expect(json.contains(#""close":["41380","10999","11999"]"#))
    }

    @Test func fullConfig() throws {
        let s24 = String(repeating: "N", count: 24)
        let stop = ConfigStop(type: .train, route: "1234567", dir: s24, id: "1234567", name: s24)
        let m = ConfigMessage(stops: [stop, stop, stop], pet: PetConfig(name: s24, colors: [0xFFFFFF, 0xFFFFFF]),
                              settings: DeviceSettings(screen: 100, led: 100, nightStart: 1439, nightEnd: 1439))
        let data = try #require(BLEMessage.encode(m))
        #expect(data.count <= 512)
        #expect(String(decoding: data, as: UTF8.self).contains(#""night_start":1439"#))
    }

    @Test func configWithRealLongNamesFitsAfterTruncation() throws {
        let long = ConfigStop(type: .train, route: "Red", dir: "Service toward 95th/Dan Ryan",
                              id: "30255", name: "Harold Washington Library-State/Van Buren")
        #expect(long.name == "Harold Washington Librar")
        #expect(long.dir == "Service toward 95th/Dan ")
        let pet = PetConfig(name: "A really long pet name here", colors: [0xFFFFFF, 0xFFFFFF])
        #expect(pet.name.utf8.count == 16)
        let m = ConfigMessage(stops: [long, long, long], pet: pet,
                              settings: DeviceSettings(screen: 100, led: 100, nightStart: 1439, nightEnd: 1439))
        #expect(try #require(BLEMessage.encode(m)).count <= 512)
    }

    @Test func configSubsetOmitsMissingKeys() throws {
        let m = ConfigMessage(stops: nil, pet: PetConfig(name: "Rattler", colors: [0xC60C30, 0x565A5C]), settings: nil)
        let json = String(decoding: try #require(BLEMessage.encode(m)), as: UTF8.self)
        #expect(!json.contains("stops") && !json.contains("settings"))
    }

    @Test func overLimitIsRejected() {
        let m = NearbyMessage(lat: 0, lon: 0, trains: Array(repeating: "41380", count: 100), buses: [], close: [])
        #expect(BLEMessage.encode(m) == nil)
    }
}
