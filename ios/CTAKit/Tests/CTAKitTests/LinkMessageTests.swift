import Foundation
import Testing
@testable import CTAKit

// The HTTP link's bodies (ecosystem spec 4.4) and the vehicle on predictions (5.7).
struct LinkMessageTests {
    @Test func trainPredictionKeepsRn() throws {
        let preds = try TrainParse.arrivals(fixture("train_arrivals_station.json"), fetched: chicago("2026-11-05T08:00:00"))
        #expect(preds.map(\.vehicle) == ["811", "912", "813", "416", "509"])
    }

    @Test func busPredictionKeepsVid() throws {
        let preds = try BusParse.predictions(fixture("bus_predictions.json"), fetched: chicago("20261105 08:00"))
        #expect(preds.map(\.vehicle) == ["1801", "1802", "1803", "1804"])
    }

    @Test func infoDecodesSnakeCase() throws {
        let json = #"""
        {"type":"handheld","id":"a1b2","fw":"0.4.0","battery":87,"charging":false,
         "wifi":{"connected":true,"ssid":"Net"},"last_update":1700000000,"uptime":42,
         "picks_ver":3,"wake":7,"docked":true}
        """#
        let info = try JSONDecoder().decode(InfoMessage.self, from: Data(json.utf8))
        #expect(info.type == .handheld && info.id == "a1b2" && info.fw == "0.4.0")
        #expect(info.battery == 87 && !info.charging)
        #expect(info.wifi == WifiState(connected: true, ssid: "Net"))
        #expect(info.lastUpdate == 1700000000 && info.uptime == 42)
        #expect(info.picksVer == 3 && info.wake == 7 && info.docked)
    }

    @Test func dockInfoDecodesNullBattery() throws {
        let json = #"""
        {"type":"dock","id":"c3d4","fw":"0.4.0","battery":null,"charging":false,
         "wifi":{"connected":true,"ssid":"Net"},"last_update":0,"uptime":42,"picks_ver":3,"wake":0,"docked":false}
        """#
        let info = try JSONDecoder().decode(InfoMessage.self, from: Data(json.utf8))
        #expect(info.type == .dock && info.battery == nil)
    }

    @Test func followEncodesFlat() throws {
        let stop = ConfigStop(type: .train, route: "Red", dir: "95th/Dan Ryan", id: "41320", name: "Belmont")
        let m = FollowMessage(stop: stop, vehicle: "812", dest: "95th/Dan Ryan")
        let data = try #require(BLEMessage.encode(m))
        let obj = try #require(JSONSerialization.jsonObject(with: data) as? [String: String])
        #expect(obj == ["type": "train", "route": "Red", "dir": "95th/Dan Ryan", "id": "41320", "name": "Belmont",
                        "vehicle": "812", "dest": "95th/Dan Ryan"])
    }

    @Test func followCutsToDeviceSizes() {
        let stop = ConfigStop(type: .bus, route: "22", dir: "Southbound", id: "1001", name: "Clark & Belmont")
        let m = FollowMessage(stop: stop, vehicle: "123456789", dest: String(repeating: "D", count: 40))
        #expect(m.vehicle == "1234567" && m.dest.utf8.count == 24)
    }

    @Test func tokenIs32LowerHex() {
        let a = Token.random(), b = Token.random()
        for t in [a, b] {
            #expect(t.count == 32)
            #expect(t.allSatisfy { "0123456789abcdef".contains($0) })
        }
        #expect(a != b)
    }

    @Test func tokenNeverInDescription() throws {
        let token = Token.random()
        let m = TokenMessage(token: token)
        for text in [String(describing: m), String(reflecting: m), "\(m)", "\([m])"] {
            #expect(!text.contains(token), "\(text)")
        }
        var dumped = ""
        dump(m, to: &dumped)
        #expect(!dumped.contains(token))
        let data = try #require(BLEMessage.encode(m))
        let obj = try #require(JSONSerialization.jsonObject(with: data) as? [String: String])
        #expect(obj == ["token": token])
    }
}
