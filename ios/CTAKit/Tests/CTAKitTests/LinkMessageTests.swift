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

    @Test func pinTrainByStationWithLineLabel() {
        let belmont = Stop(id: "41320", name: "Belmont", lat: 0, lon: 0, kind: .station, lines: nil,
                           platforms: [Platform(id: "30256", lines: [Line(route: "Red", dir: "95th/Dan Ryan")])])
        let pin = ConfigStop(pinning: belmont, line: Line(route: "Red", dir: "95th/Dan Ryan"))
        #expect(pin == ConfigStop(type: .train, route: "Red", dir: "95th/Dan Ryan", id: "41320", name: "Belmont"))
    }

    @Test func followNeedsAVehicle() throws {
        let line = Line(route: "22", dir: "Southbound")
        let stop = Stop(id: "1001", name: "Clark & Belmont", lat: 0, lon: 0, kind: .bus, lines: [line], platforms: nil)
        func departure(_ vehicle: String?) -> Prediction {
            Prediction(stopID: "1001", route: "22", dir: "Southbound", dest: "Harrison", at: .now, live: true,
                       delayed: false, vehicle: vehicle)
        }
        let f = try #require(FollowMessage(stop: stop, line: line, departure: departure("1801")))
        #expect(f.stop == ConfigStop(type: .bus, route: "22", dir: "Southbound", id: "1001", name: "Clark & Belmont"))
        #expect(f.vehicle == "1801" && f.dest == "Harrison")
        #expect(FollowMessage(stop: stop, line: line, departure: departure(nil)) == nil)
        #expect(FollowMessage(stop: stop, line: line, departure: departure("")) == nil)
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

    // The wearable BLE link (plan 2026-11-14-wearable-ble-link, K1). Fixed JSON so the firmware's test_link can match.

    func sameJSON(_ data: Data, _ text: String) throws -> Bool {
        let a = try JSONSerialization.jsonObject(with: data) as? NSDictionary
        let b = try JSONSerialization.jsonObject(with: Data(text.utf8)) as? NSDictionary
        return a != nil && a == b
    }

    @Test func infoDecodesDockWithoutNewFields() throws {
        let json = #"""
        {"type":"dock","id":"c3d4","fw":"0.4.0","battery":null,"charging":false,
         "wifi":{"connected":true,"ssid":"Net"},"last_update":0,"uptime":42,"picks_ver":3,"wake":0,"docked":false}
        """#
        let info = try JSONDecoder().decode(InfoMessage.self, from: Data(json.utf8))
        #expect(info.wifi.rssi == 0 && !info.paired && !info.relaying && info.settings == nil && info.pin == nil)
    }

    @Test func infoDecodesWearableWithRSSI() throws {
        let json = #"""
        {"type":"handheld","id":"a1b2","fw":"0.5.0","battery":64,"charging":true,
         "wifi":{"connected":true,"ssid":"Net","rssi":-61},"last_update":1763000000,"uptime":42,
         "picks_ver":0,"wake":7,"docked":false,"paired":true,
         "settings":{"screen":80,"led":50,"night_start":60,"night_end":300},"relaying":true,"pin":"30254"}
        """#
        let info = try JSONDecoder().decode(InfoMessage.self, from: Data(json.utf8))
        #expect(info.battery == 64 && info.charging)
        #expect(info.wifi == WifiState(connected: true, ssid: "Net", rssi: -61))
        #expect(info.paired && info.relaying && info.pin == "30254")
        let unpinned = json.replacingOccurrences(of: #""pin":"30254""#, with: #""pin":null"#)
        #expect(try JSONDecoder().decode(InfoMessage.self, from: Data(unpinned.utf8)).pin == nil)
        #expect(info.settings == DeviceSettings(screen: 80, led: 50, nightStart: 60, nightEnd: 300))
    }

    @Test func relayTickEncodes() throws {
        let full = try #require(BLEMessage.encode(RelayTick(now: 1_763_000_000, wmo: 3, temp: 21.5)))
        #expect(try sameJSON(full, #"{"now":1763000000,"wmo":3,"temp":21.5}"#))
        let bare = try #require(BLEMessage.encode(RelayTick(now: 1_763_000_000, wmo: nil, temp: nil)))
        #expect(String(decoding: bare, as: UTF8.self) == #"{"now":1763000000}"#)
    }

    var relayRecord: StopRecord {
        StopRecord(id: "40380", type: .train, route: "Blue", dir: "O'Hare", name: "Clark/Lake",
                   arrivals: [Arrival(min: 2, dest: "O'Hare", live: true, delayed: false, veh: "123"),
                              Arrival(min: 9, dest: "O'Hare", live: false, delayed: true)],
                   alert: false, ts: 1_763_000_000)
    }

    @Test func relayStopEncodes() throws {
        let data = try #require(BLEMessage.encode(RelayStop(id: "40380", type: .train, records: [relayRecord]),
                                                  limit: BLEFrames.maxMessage))
        #expect(try sameJSON(data, #"""
        {"id":"40380","type":"train","records":[{"id":"40380","type":"train","route":"Blue","dir":"O'Hare",
         "name":"Clark/Lake","arrivals":[{"min":2,"dest":"O'Hare","live":true,"delayed":false,"veh":"123"},
         {"min":9,"dest":"O'Hare","live":false,"delayed":true}],"alert":false,"ts":1763000000}]}
        """#))
    }

    @Test func relayStopRoundTrip() throws {
        let records = (0..<6).map { i -> StopRecord in var r = relayRecord; r.dir = "Dir \(i)"; return r }
        let data = try #require(BLEMessage.encode(RelayStop(id: "40380", type: .train, records: records),
                                                  limit: BLEFrames.maxMessage))
        let obj = try #require(JSONSerialization.jsonObject(with: data) as? [String: Any])
        let list = try #require(obj["records"] as? [Any])
        let back = try list.map { try #require(StopRecord.decode(JSONSerialization.data(withJSONObject: $0))) }
        #expect(back == records)
    }

    @Test func relayStopCapsAt16AndCutsID() {
        let m = RelayStop(id: "123456789", type: .bus, records: Array(repeating: relayRecord, count: 20))
        #expect(m.records.count == Config.maxRelayRecords && m.id == "1234567")
    }

    @Test func relayStopTooBigDoesNotEncode() {
        var r = relayRecord
        r.name = String(repeating: "\u{01}", count: 24)   // 6 bytes each once escaped
        r.dir = r.name
        r.arrivals = Array(repeating: Arrival(min: 1, dest: r.name, live: true, delayed: false, veh: "1234567"), count: 4)
        #expect(BLEMessage.encode(RelayStop(id: "1", type: .train, records: Array(repeating: r, count: 16)),
                                  limit: BLEFrames.maxMessage) == nil)
    }
}
