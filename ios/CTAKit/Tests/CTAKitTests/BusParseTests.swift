import Foundation
import Testing
@testable import CTAKit

// Mirrors firmware test_bus (plan 1, task 5) through parse → StopRecord.make.
struct BusParseTests {
    let now = chicago("20261105 08:00")

    @Test func predictionsMirrorCpp() throws {
        let preds = try BusParse.predictions(fixture("bus_predictions.json"), fetched: now)
        let recs = records(preds, kind: .bus, fetched: now)
        #expect(recs.count == 2)
        #expect(recs[0].id == "1001")
        #expect(recs[0].type == .bus)
        #expect(recs[0].arrivals.count == 3)
        #expect(recs[0].arrivals[0].min == 0)   // 36, DUE
        #expect(recs[0].arrivals[1].min == 4)
        #expect(recs[0].arrivals[2].delayed)    // DLY
        #expect(recs[0].route == "36" && recs[0].dir == "Southbound")
        #expect(recs[1].dir == "Westbound")
        #expect(recs.allSatisfy { $0.arrivals.allSatisfy(\.live) })
    }

    @Test func routeFilter() throws {
        let preds = try BusParse.predictions(fixture("bus_predictions.json"), fetched: now).filter { $0.route == "22" }
        let recs = records(preds, kind: .bus, fetched: now)
        #expect(recs.count == 1)
        #expect(recs[0].arrivals.count == 2)
    }

    @Test func noArrivalTimesIsEmptyNotAnError() throws {
        #expect(try BusParse.predictions(fixture("bus_no_arrivals.json"), fetched: now).isEmpty)
    }

    @Test func keyErrorThrows() throws {
        #expect(throws: CTAError.api(code: "bus", message: "Invalid API access key supplied")) {
            try BusParse.predictions(fixture("bus_error_key.json"), fetched: now)
        }
    }

    @Test func malformedThrows() {
        #expect(throws: (any Error).self) { try BusParse.predictions(Data("[1,2".utf8), fetched: now) }
    }

    @Test func vehiclesAcceptNumbersAsStrings() throws {
        let v = try BusParse.vehicles(fixture("bus_vehicles.json"))
        #expect(v.count == 3)
        #expect(v[1].pid == 100)
        #expect(v[1].pdist == 1500)
        #expect(v[0].id == "1801" && v[0].heading == 180 && v[0].dest == "Harrison")
    }

    @Test func patterns() throws {
        let p = try BusParse.patterns(fixture("bus_patterns.json"))
        #expect(p.map(\.pid) == [100, 200])
        #expect(p[0].points.count == 11)
        #expect(p[0].points.compactMap(\.stopID) == (2001...2010).map(String.init))
        #expect(p[0].points[4].stopID == nil)   // the waypoint
        #expect(p[1].points.map(\.pdist) == [0, 500, 1000])
    }

    func prd(_ fields: String) -> Data {
        Data(#"{"bustime-response":{"prd":[{"tmstmp":"20261105 08:00","stpid":"1001","rt":"22","rtdir":"Southbound","des":"Harrison",\#(fields)}]}}"#.utf8)
    }

    @Test func dlyCountdownAloneMeansDelayed() throws {
        let p = try BusParse.predictions(prd(#""prdtm":"20261105 08:15","prdctdn":"DLY","dly":false"#), fetched: now)
        #expect(p[0].delayed)
    }

    @Test func dueIsTheFetchTime() throws {
        let p = try BusParse.predictions(prd(#""prdtm":"20261105 08:02","prdctdn":"DUE","dly":false"#), fetched: now)
        #expect(p[0].at == now)
    }

    @Test func errorNamingOnlyAPatternIsEmpty() throws {
        let json = #"{"bustime-response":{"error":[{"pid":"100","msg":"No data found for parameter"}]}}"#
        #expect(try BusParse.patterns(Data(json.utf8)).isEmpty)
    }

    @Test func vehicleWithBadCoordinatesIsDroppedNotFatal() throws {
        let json = #"{"bustime-response":{"vehicle":[{"vid":"1","lat":"","lon":"-87.6","hdg":"90","pid":100,"des":"Harrison","pdist":10},{"vid":"2","lat":"41.9","lon":"-87.6","hdg":"90","pid":"100","des":"Harrison","pdist":"20"}]}}"#
        let v = try BusParse.vehicles(Data(json.utf8))
        #expect(v.map(\.id) == ["2"])
    }

    @Test func patternPointWithoutCoordinatesIsDroppedNotFatal() throws {
        let json = #"{"bustime-response":{"ptr":[{"pid":100,"rtdir":"Southbound","pt":[{"typ":"W","pdist":5},{"lat":41.9,"lon":-87.6,"typ":"S","stpid":"2001","stpnm":"A","pdist":0}]}]}}"#
        let p = try BusParse.patterns(Data(json.utf8))
        #expect(p[0].points.compactMap(\.stopID) == ["2001"])
    }
}
