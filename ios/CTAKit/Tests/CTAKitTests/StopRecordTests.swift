import Foundation
import Testing
@testable import CTAKit

// Mirrors firmware test_stop_record (plan 1, task 3).
struct StopRecordTests {
    let s24 = String(repeating: "N", count: 24)

    var worst: StopRecord {
        StopRecord(id: "4132099", type: .train, route: "1234567", dir: s24, name: s24,
                   arrivals: Array(repeating: Arrival(min: 999, dest: s24, live: false, delayed: false), count: 4),
                   alert: false, ts: 4_294_967_295)
    }

    @Test func roundTrip() throws {
        let r = StopRecord(id: "30255", type: .train, route: "Red", dir: "95th/Dan Ryan", name: "Belmont",
                           arrivals: [Arrival(min: 3, dest: "95th/Dan Ryan", live: true, delayed: false),
                                      Arrival(min: 12, dest: "Howard", live: false, delayed: true)],
                           alert: true, ts: 1_763_000_000)
        let data = try #require(r.encoded())
        #expect(StopRecord.decode(data) == r)
        #expect(!String(decoding: data, as: UTF8.self).contains("\\/"))   // no escaped slashes wasting bytes
    }

    @Test func truncation() {
        #expect(("Harlem/Lake (via Marketplace)").truncatedUTF8(24) == "Harlem/Lake (via Marketp")
        #expect((String(repeating: "A", count: 23) + "é").truncatedUTF8(24) == String(repeating: "A", count: 23))
        #expect(("Belmont").truncatedUTF8(24) == "Belmont")
        #expect(("ééé").truncatedUTF8(4) == "éé")
        #expect(s24.truncatedUTF8(24) == s24)   // exactly at the limit
    }

    @Test func makeTruncatesNames() {
        let p = Prediction(stopID: "30255", route: "Red", dir: "", dest: "Harlem/Lake (via Marketplace)",
                           at: Date(timeIntervalSince1970: 600), live: true, delayed: false)
        let r = StopRecord.make(id: "30255", kind: .station, name: "Harlem/Lake (via Marketplace)",
                                from: [p], alert: false, fetched: Date(timeIntervalSince1970: 0))
        #expect(r.name == "Harlem/Lake (via Marketp")
        #expect(r.arrivals[0].dest == "Harlem/Lake (via Marketp")
        #expect(r.arrivals[0].min == 10)
    }

    @Test func makeTakesRouteAndDirFromTheSoonest() {
        func p(_ route: String, _ dir: String, _ at: TimeInterval) -> Prediction {
            Prediction(stopID: "1", route: route, dir: dir, dest: "", at: Date(timeIntervalSince1970: at), live: true, delayed: false)
        }
        let r = StopRecord.make(id: "1", kind: .bus, name: "", from: [p("22", "Northbound", 600), p("36", "Southbound", 120)],
                                alert: false, fetched: Date(timeIntervalSince1970: 0))
        #expect(r.route == "36" && r.dir == "Southbound")
    }

    @Test func makeClampsPastArrivalsToZero() {
        let p = Prediction(stopID: "1", route: "22", dir: "Southbound", dest: "Harrison",
                           at: Date(timeIntervalSince1970: 0), live: true, delayed: false)
        let r = StopRecord.make(id: "1", kind: .bus, name: "", from: [p], alert: false, fetched: Date(timeIntervalSince1970: 90))
        #expect(r.arrivals[0].min == 0)
    }

    @Test func worstCaseFits() throws {
        let data = try #require(worst.encoded())
        #expect(data.count <= 512)
    }

    @Test func overLimitDoesNotEncode() {
        var r = worst
        r.name = String(repeating: "\u{01}", count: 24)   // each escapes to 6 bytes
        r.dir = r.name
        #expect(r.encoded() == nil)
    }

    @Test func decodeKeepsFourArrivalsAndTruncates() throws {
        let arrivals = (0..<6).map { #"{"min":\#($0),"dest":"Harlem/Lake (via Marketplace)","live":true,"delayed":false}"# }
        let json = #"{"id":"30255","type":"train","route":"Red","dir":"","name":"Harlem/Lake (via Marketplace)","arrivals":[\#(arrivals.joined(separator: ","))],"alert":false,"ts":1}"#
        let r = try #require(StopRecord.decode(Data(json.utf8)))
        #expect(r.arrivals.count == 4)
        #expect(r.name == "Harlem/Lake (via Marketp")
        #expect(r.arrivals[0].dest == "Harlem/Lake (via Marketp")
    }

    @Test func decodeRejectsMalformedOrMissingID() {
        #expect(StopRecord.decode(Data(#"{"id":"#.utf8)) == nil)
        #expect(StopRecord.decode(Data(#"{"type":"train","route":"Red","dir":"","name":"","arrivals":[],"alert":false,"ts":1}"#.utf8)) == nil)
    }
}
