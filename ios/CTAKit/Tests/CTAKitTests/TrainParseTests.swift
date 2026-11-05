import Foundation
import Testing
@testable import CTAKit

// Mirrors firmware test_train (plan 1, task 4) through parse → StopRecord.make.
struct TrainParseTests {
    let now = chicago("2026-11-05T08:00:00")

    @Test func stationArrivalsMirrorCpp() throws {
        let preds = try TrainParse.arrivals(fixture("train_arrivals_station.json"), fetched: now)
        let r = StopRecord.make(id: "41320", kind: .station, name: "Belmont", from: preds, alert: false, fetched: now)
        #expect(r.arrivals.count == 4)   // Linden at 20 min dropped
        #expect(r.arrivals.map(\.min) == [1, 3, 7, 12])
        #expect(r.arrivals[0].dest == "Kimball")
        #expect(r.arrivals[1].live)
        #expect(r.arrivals[2].delayed)
        #expect(!r.arrivals[3].live)
        #expect(r.name == "Belmont")
        #expect(r.id == "41320")
        #expect(r.type == .train)
        #expect(r.route == "Brn")   // rt of the soonest arrival
        #expect(r.dir == "")        // a station record has an empty dir
        #expect(r.ts == Int(now.timeIntervalSince1970))
    }

    @Test func multiLineStationKeepsEachPredictionsRouteAndPlatform() throws {
        let preds = try TrainParse.arrivals(fixture("train_arrivals_station.json"), fetched: now)
        #expect(preds.count == 5)   // the parser doesn't cap; only the record does
        #expect(preds.map(\.route) == ["Red", "Red", "Red", "Brn", "P"])
        #expect(preds.map(\.stopID) == ["30255", "30256", "30255", "30257", "30257"])
        #expect(preds.allSatisfy { $0.dir.isEmpty })
        #expect(preds[1].dest == "Howard" && !preds[1].live)
    }

    @Test func midnightArrival() throws {
        let fetched = chicago("2026-11-05T23:58:30")
        let preds = try TrainParse.arrivals(fixture("train_arrivals_midnight.json"), fetched: fetched)
        let r = StopRecord.make(id: "41320", kind: .station, name: "Belmont", from: preds,
                                alert: false, fetched: fetched)
        #expect(r.arrivals.map(\.min) == [5])
    }

    @Test func badKeyThrows() throws {
        #expect(throws: CTAError.api(code: "101", message: "Invalid API key.")) {
            try TrainParse.arrivals(fixture("train_error.json"), fetched: now)
        }
    }

    @Test func malformedThrows() {
        #expect(throws: (any Error).self) { try TrainParse.arrivals(Data("{\"ctatt\":".utf8), fetched: now) }
    }

    @Test func positions() throws {
        let red = try TrainParse.positions(fixture("train_positions_red.json"))
        #expect(red.count == 2)
        #expect(red[0].nextStopID == "30005")
        #expect(red[0].id == "811" && red[0].dest == "95th/Dan Ryan" && red[0].nextStop == "Wilson")
        #expect(red[0].heading == 178)
        let single = try TrainParse.positions(fixture("train_positions_single.json"))   // object, not array
        #expect(single.count == 1)
        #expect(single[0].nextStopID == "30005")
    }

    @Test func fallBackNightReadsMinutesNotDue() throws {
        // 1:58 AM in the repeated hour on Nov 1 2026 is 07:58 UTC (standard time).
        let json = #"{"ctatt":{"tmst":"2026-11-01T01:58:00","errCd":"0","errNm":null,"eta":[{"stpId":"30255","rt":"Red","destNm":"95th/Dan Ryan","arrT":"2026-11-01T01:59:30","isSch":"0","isDly":"0"}]}}"#
        let fetched = Date(timeIntervalSince1970: 1_793_519_880)
        let preds = try TrainParse.arrivals(Data(json.utf8), fetched: fetched)
        #expect(ArrivalFormat.label(preds[0], now: fetched) == "1 min")
        #expect(ArrivalFormat.label(preds[0], now: fetched.addingTimeInterval(30)) == "1 min")
    }

    @Test func missingTimestampThrows() {
        let json = #"{"ctatt":{"errCd":"0","eta":[{"stpId":"30255","rt":"Red","destNm":"Howard","arrT":"2026-11-05T08:03:00","isSch":"0","isDly":"0"}]}}"#
        #expect(throws: (any Error).self) { try TrainParse.arrivals(Data(json.utf8), fetched: now) }
    }

    @Test func trainWithBadCoordinatesIsDroppedNotFatal() throws {
        let json = #"{"ctatt":{"errCd":"0","route":[{"@name":"red","train":[{"rn":"1","destNm":"Howard","lat":"","lon":"-87.6","heading":"0"},{"rn":"2","destNm":"Howard","lat":41.9,"lon":"-87.6"},{"rn":"3","destNm":"Howard","lat":"91","lon":"-87.6"}]}]}}"#
        let v = try TrainParse.positions(Data(json.utf8))
        #expect(v.map(\.id) == ["2"])
        #expect(v[0].heading == 0)
    }
}
