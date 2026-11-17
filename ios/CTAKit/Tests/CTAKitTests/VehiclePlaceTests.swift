import CoreLocation
import Foundation
import Testing
@testable import CTAKit

/// Reads `data/<name>` from the repo root, the app's bundled data.
private func data(_ name: String) throws -> Data {
    try Data(contentsOf: URL(filePath: #filePath).deletingLastPathComponent()
        .appending(path: "../../../../data/\(name)").standardized)
}

// F6: matching an arrival to its vehicle, where the vehicle is in words, and a focus that drops out of the feed.
struct VehiclePlaceTests {
    static let runs = try! TrainRun.decode(data("train_lines.json"))
    static let index = try! StopIndex(data: data("stops.json"))
    let coordinate: (String) -> CLLocationCoordinate2D? = { id in
        VehiclePlaceTests.index.station(ofPlatform: id).map { CLLocationCoordinate2D(latitude: $0.lat, longitude: $0.lon) }
    }

    func train(next: String, lat: Double, lon: Double, dest: String = "Loop") -> Vehicle {
        Vehicle(id: "411", lat: lat, lon: lon, heading: 180, dest: dest, nextStopID: next, nextStop: "",
                pid: nil, pdist: nil)
    }

    /// A train halfway from platform `from` to platform `to`, its next stop.
    func halfway(_ from: String, _ to: String, dest: String = "Loop") -> Vehicle {
        let (a, b) = (station(from), station(to))
        return train(next: to, lat: (a.lat + b.lat) / 2, lon: (a.lon + b.lon) / 2, dest: dest)
    }

    func place(_ v: Vehicle, _ route: String, _ target: String) -> VehiclePlace? {
        VehiclePlace.train(v, route: route, target: target, runs: Self.runs, coordinate: coordinate)
    }

    func station(_ platform: String) -> Stop { Self.index.station(ofPlatform: platform)! }

    // MARK: Matching

    @Test func trainArrivalMatchesItsRunNumber() throws {
        let preds = try TrainParse.arrivals(fixture("train_arrivals_belmont_live.json"), fetched: .now)
        let vehicles = [train(next: "30232", lat: 41.94, lon: -87.65),
                        Vehicle(id: "815", lat: 41.93, lon: -87.65, heading: 0, dest: "Howard", nextStopID: "30255",
                                nextStop: "Belmont", pid: nil, pdist: nil)]
        #expect(preds[0].vehicle == "411" && preds[0].vehicle(in: vehicles)?.id == "411")
        #expect(preds[1].vehicle(in: vehicles)?.dest == "Howard")
        #expect(preds[2].vehicle(in: vehicles) == nil)   // run 510 isn't in the feed
    }

    @Test func busPredictionMatchesItsVehicleID() throws {
        let preds = try BusParse.predictions(fixture("bus_predictions.json"), fetched: .now)
        let vehicles = try BusParse.vehicles(fixture("bus_vehicles.json"))
        #expect(preds[0].vehicle(in: vehicles)?.pdist == 4500)   // vid 1801
        #expect(preds[1].vehicle(in: vehicles) == nil)            // vid 1802 isn't on the road
        let noID = Prediction(stopID: "1", route: "22", dir: "", dest: "", at: .now, live: true, delayed: false)
        #expect(noID.vehicle(in: vehicles) == nil)
    }

    // MARK: Trains

    @Test func trainBetweenStationsCountsStopsToTheOpenOne() throws {
        // A Loop-bound Brown train halfway from Belmont to Wellington; the open stop is Fullerton (Loop-bound).
        let (b, w) = (station("30258"), station("30232"))
        let v = train(next: "30232", lat: (b.lat + w.lat) / 2, lon: (b.lon + w.lon) / 2)
        let place = try #require(VehiclePlace.train(v, route: "Brn", target: "30236", runs: Self.runs, coordinate: coordinate))
        #expect(place.text == "Between Belmont and Wellington, 3 stops away")
        #expect(place.toward)
    }

    @Test func trainAtAStation() throws {
        let f = station("30236")
        let v = train(next: "30236", lat: f.lat + 0.0005, lon: f.lon)   // about 55 m off
        #expect(VehiclePlace.train(v, route: "Brn", target: "30128", runs: Self.runs, coordinate: coordinate)?.text
                == "At Fullerton, 1 stop away")
        #expect(VehiclePlace.train(v, route: "Brn", target: "30236", runs: Self.runs, coordinate: coordinate)?.text
                == "At Fullerton")
        // Still at the station it just served: its next stop is the one after.
        let leaving = train(next: "30128", lat: f.lat, lon: f.lon)
        #expect(VehiclePlace.train(leaving, route: "Brn", target: "30128", runs: Self.runs, coordinate: coordinate)?.text
                == "At Fullerton, 1 stop away")
        #expect(place(leaving, "Brn", "30236")?.text == "At Fullerton")
        #expect(place(leaving, "Brn", "30236")?.toward == true)
    }

    @Test func trainNotHeadedForTheStopHasNoCount() throws {
        let v = halfway("30258", "30232")
        // Belmont toward the Loop is behind it, and Brown trains end at Kimball, so it isn't reached again.
        let behind = try #require(place(v, "Brn", "30258"))
        #expect(behind.text == "Between Belmont and Wellington")
        #expect(!behind.toward)
        // Belmont toward Kimball comes after the Loop.
        #expect(place(v, "Brn", "30257")?.text == "Between Belmont and Wellington, 22 stops away")
        // Bound for Kimball already, the Loop is behind it.
        let home = halfway("30231", "30257", dest: "Kimball")
        #expect(place(home, "Brn", "30075")?.toward == false)
        #expect(place(home, "Brn", "30249")?.text == "Between Wellington and Belmont, 12 stops away")
    }

    // MARK: Runs that start or end mid-trip

    @Test func purpleExpressBetweenHowardAndWilson() {
        // The Express run starts at Wilson; Howard is the end of the shuttle's run.
        #expect(place(halfway("30176", "30385"), "P", "30258")?.text == "Between Howard and Wilson, 2 stops away")
        let w = station("30385")
        #expect(place(train(next: "30385", lat: w.lat, lon: w.lon), "P", "30258")?.text == "At Wilson, 1 stop away")
        // Northbound, Wilson's run ends; Howard and the shuttle's stops follow.
        #expect(place(halfway("30386", "30175", dest: "Linden"), "P", "30203")?.text
                == "Between Wilson and Howard, 9 stops away")
        // A shuttle ends at Howard: Belmont isn't ahead of it.
        #expect(place(halfway("30164", "30176", dest: "Howard"), "P", "30258")?.toward == false)
    }

    @Test func runsThatTurnAtTheLoopCountOnPastIt() {
        // The Loop runs end before it; the next runs start a few stops earlier.
        #expect(place(halfway("30232", "30104"), "Brn", "30075")?.text == "Between Wellington and Diversey, 13 stops away")
        #expect(place(halfway("30185", "30219"), "Org", "30074")?.text == "Between Pulaski and Kedzie, 11 stops away")
        #expect(place(halfway("30143", "30040"), "Pink", "30074")?.text == "Between Western and Damen, 7 stops away")
    }

    @Test func greenBranchesFollowOnFromTheTrunk() {
        // Garfield to King Drive, the Cottage Grove branch's first stop.
        #expect(place(halfway("30100", "30217", dest: "Cottage Grove"), "G", "30139")?.text
                == "Between Garfield and King Drive, 2 stops away")
        // Southbound in the Loop: Ashland/63rd is ahead only of the trains going there.
        let toAshland = halfway("30384", "30132", dest: "Ashland/63rd")
        #expect(place(toAshland, "G", "30057")?.text == "Between Washington/Wabash and Adams/Wabash, 11 stops away")
        #expect(place(toAshland, "G", "30139")?.toward == false)
        let toCottage = halfway("30384", "30132", dest: "Cottage Grove")
        #expect(place(toCottage, "G", "30057")?.toward == false)
        #expect(place(toCottage, "G", "30139")?.text == "Between Washington/Wabash and Adams/Wabash, 11 stops away")
        #expect(place(toCottage, "G", "30100")?.away == 9)   // Garfield, before they split
        // Northbound from the Ashland branch, onto the trunk at Garfield.
        #expect(place(halfway("30183", "30099", dest: "Harlem/Lake"), "G", "30024")?.text
                == "Between Halsted and Garfield, 2 stops away")
        // King Drive's run northbound passes Garfield too; the nearer stop before it is named.
        #expect(place(halfway("30218", "30099", dest: "Harlem/Lake"), "G", "30024")?.text
                == "Between King Drive and Garfield, 2 stops away")
    }

    @Test func aLineStartOnlyCountsAsAtItUpClose() {
        // Kimball starts the Loop-bound run: no stop before it is known.
        let k = station("30250")
        #expect(place(train(next: "30250", lat: k.lat + 0.01, lon: k.lon), "Brn", "30226")?.text
                == "Approaching Kimball, 2 stops away")
        #expect(place(train(next: "30250", lat: k.lat + 0.0005, lon: k.lon), "Brn", "30226")?.text
                == "At Kimball, 1 stop away")
    }

    @Test func trainRunThatReachesTheStopSoonestWins() throws {
        // Loop-bound at Diversey: Belmont toward Kimball is only reached after the Loop, in the Kimball run.
        let d = station("30104")
        let v = train(next: "30104", lat: d.lat, lon: d.lon)
        let place = try #require(VehiclePlace.train(v, route: "Brn", target: "30257", runs: Self.runs, coordinate: coordinate))
        #expect(place.toward && place.away! > 10)
        #expect(place.text.hasPrefix("At Diversey, "))
    }

    @Test func branchNamesAreShortened() {
        #expect(VehiclePlace.short("Harlem (Blue - O'Hare Branch)") == "Harlem")
        #expect(VehiclePlace.short("Belmont") == "Belmont")
    }

    @Test func unknownNextStopHasNoPlace() {
        let v = train(next: "99999", lat: 41.9, lon: -87.6)
        #expect(VehiclePlace.train(v, route: "Brn", target: "30236", runs: Self.runs, coordinate: coordinate) == nil)
    }

    // MARK: Buses

    @Test func busesByDistanceAlongTheirPattern() throws {
        let patterns = try BusParse.patterns(fixture("bus_patterns.json"))
        let buses = try BusParse.vehicles(fixture("bus_vehicles.json"))
        // 1801 at 4500 ft: 2005 is at 4000, 2006 at 5000; the open stop 2008 is 3 stops on.
        #expect(VehiclePlace.bus(buses[0], target: "2008", patterns: patterns)?.text
                == "Between Stop 2005 and Stop 2006, 3 stops away")
        #expect(VehiclePlace.bus(buses[1], target: "2003", patterns: patterns)?.text
                == "Between Stop 2002 and Stop 2003, 1 stop away")
        // Past the end of its short pattern: at its last stop.
        #expect(VehiclePlace.bus(buses[2], target: "2008", patterns: patterns)?.text == "At Stop 2009")
        let near = Vehicle(id: "9", lat: 0, lon: 0, heading: 0, dest: "", nextStopID: "", nextStop: "", pid: 100, pdist: 4150)
        #expect(VehiclePlace.bus(near, target: "2005", patterns: patterns)?.text == "At Stop 2005")
        let unknown = Vehicle(id: "9", lat: 0, lon: 0, heading: 0, dest: "", nextStopID: "", nextStop: "", pid: 7, pdist: 0)
        #expect(VehiclePlace.bus(unknown, target: "2005", patterns: patterns) == nil)
    }

    // MARK: A focus that drops out

    @Test func focusSaysWhyThereIsNoPosition() {
        let v = train(next: "30232", lat: 41.9, lon: -87.6)
        #expect(VehicleFocus.of("411", vehicles: nil, seen: false, failing: false) == .finding)
        #expect(VehicleFocus.of("411", vehicles: nil, seen: true, failing: true) == .unavailable)
        #expect(VehicleFocus.of("411", vehicles: [v], seen: false, failing: false) == .found(v))
        #expect(VehicleFocus.of("411", vehicles: [v], seen: true, failing: true) == .found(v))   // last good fetch
        #expect(VehicleFocus.of("999", vehicles: [v], seen: false, failing: false) == .notYet)
        #expect(VehicleFocus.of("999", vehicles: [v], seen: true, failing: false) == .lost)
        #expect(VehicleFocus.lost.text(bus: false) == "Lost this train: it stopped sharing its position")
        #expect(VehicleFocus.notYet.text(bus: true) == "This bus isn't sharing its position yet")
        #expect(VehicleFocus.found(v).text(bus: false) == nil)
    }

    // MARK: Art

    @Test func vehicleArtIsTheStopArtWithoutItsFrame() {
        #expect(StopArt.train.count == 12 && StopArt.train.allSatisfy { $0.count == 10 })
        #expect(!StopArt.train.joined().contains(7))   // no tunnel arch
        #expect(StopArt.busVehicle.count == 14 && StopArt.busVehicle.last!.contains { $0 != 0 })
    }
}
