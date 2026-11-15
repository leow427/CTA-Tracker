import CoreLocation
import Foundation
import Testing
@testable import CTAKit

// The `nearby` the app sends the wearable (ecosystem spec 5.5): from a location fix, or mirroring the phone's list.
// Every message must pass the wearable's parseNearby (nearby_msg.cpp), which drops the whole message otherwise.
struct NearbyBuilderTests {
    static let belmont = CLLocationCoordinate2D(latitude: 41.9399, longitude: -87.6533)
    static let index = CardBuilderSpeedupTests.index

    /// The checks parseNearby makes.
    static func expectValid(_ m: NearbyMessage) {
        #expect(m.trains.count <= Config.nearbyPerTab && m.buses.count <= Config.nearbyPerTab)
        #expect(m.close.count <= Config.hypedClosestStops)
        #expect(Set(m.trains).count == m.trains.count && Set(m.buses).count == m.buses.count)
        #expect(Set(m.close).count == m.close.count)
        #expect(m.trains.allSatisfy { $0.count == 5 && $0.hasPrefix("4") && $0.allSatisfy(\.isNumber) })
        #expect(m.buses.allSatisfy { (1...7).contains($0.count) && $0.allSatisfy(\.isNumber) })
        #expect(m.close.allSatisfy { m.trains.contains($0) || m.buses.contains($0) })
        #expect((-90...90).contains(m.lat) && (-180...180).contains(m.lon))
    }

    static func slot(_ fetchID: String, _ kind: Kind, route: String = "Red", dir: String = "x", meters: Double = 0) -> Slot {
        Slot(line: Line(route: route, dir: dir), stopID: kind == .station ? "3\(fetchID.dropFirst())" : fetchID,
             fetchID: fetchID, kind: kind, meters: meters)
    }

    static func list(_ slots: [Slot], favorites: [Slot] = []) -> SlotList {
        SlotList(favorites: favorites, nearby: slots, farFromChicago: false)
    }

    @Test func fromLocationMatchesSendNearby() {
        // Recorded from `python3 tools/send_nearby.py --dry-run --lat 41.9399 --lon -87.6533`; close in the order its
        // message() puts them (11, 18, 19 m), which the dry run doesn't print.
        let m = NearbyBuilder.fromLocation(Self.belmont, index: Self.index)
        #expect(m.trains == ["41320", "41210", "40530", "41420", "40360"])
        #expect(m.buses == ["17380", "17379", "1828", "1922", "14906"])
        #expect(m.close == ["17380", "41320", "17379"])
        #expect(m.lat == Self.belmont.latitude && m.lon == Self.belmont.longitude)
        Self.expectValid(m)
    }

    @Test func fromLocationTakesEachRouteDirectionsNearestBusStop() {
        func bus(_ id: String, _ lat: Double, _ routes: [String]) -> Stop {
            Stop(id: id, name: id, lat: lat, lon: -87.6533, kind: .bus,
                 lines: routes.map { Line(route: $0, dir: "Northbound") }, platforms: nil)
        }
        // 2 is only another stop for 22 and 36, both already nearest at 1; 3 brings 8.
        let index = StopIndex(stops: [bus("1", 41.9400, ["22", "36"]), bus("2", 41.9401, ["36"]), bus("3", 41.9402, ["8"])])
        let m = NearbyBuilder.fromLocation(Self.belmont, index: index)
        #expect(m.buses == ["1", "3"])
        #expect(m.close == ["1", "3"])
        Self.expectValid(m)
    }

    @Test func fromListKeepsListOrder() {
        // Favorites first, then the nearby cards, as the list shows them; not re-sorted by distance.
        let m = NearbyBuilder.fromList(Self.list([Self.slot("1828", .bus, meters: 10), Self.slot("41210", .station, meters: 5),
                                                  Self.slot("17380", .bus, meters: 1)],
                                                 favorites: [Self.slot("40360", .station, meters: 900)]),
                                       center: Self.belmont)
        #expect(m.trains == ["40360", "41210"])
        #expect(m.buses == ["1828", "17380"])
        #expect(m.lat == Self.belmont.latitude && m.lon == Self.belmont.longitude)
        Self.expectValid(m)
    }

    @Test func fromListUsesStationIDs() {
        // Belmont has a card per line and direction, all fetched by the parent station: one 41320.
        let slots = [Self.slot("41320", .station, route: "Red", dir: "Howard"),
                     Self.slot("41320", .station, route: "Red", dir: "95th/Dan Ryan"),
                     Self.slot("17380", .bus, route: "77", dir: "Westbound"),
                     Self.slot("17380", .bus, route: "77", dir: "Eastbound")]
        let m = NearbyBuilder.fromList(Self.list(slots), center: Self.belmont)
        #expect(m.trains == ["41320"])
        #expect(m.buses == ["17380"])
        #expect(m.close == ["41320", "17380"])
        Self.expectValid(m)
    }

    @Test func fromListCapsAt5Each() {
        let stations = (0..<8).map { Self.slot("4100\($0)", .station) }
        let buses = (0..<8).map { Self.slot("100\($0)", .bus) }
        let m = NearbyBuilder.fromList(Self.list(stations + buses), center: Self.belmont)
        #expect(m.trains == (0..<5).map { "4100\($0)" })
        #expect(m.buses == (0..<5).map { "100\($0)" })
        Self.expectValid(m)
    }

    @Test func closeIsFirst3OfEither() {
        let m = NearbyBuilder.fromList(Self.list([Self.slot("1001", .bus), Self.slot("41001", .station),
                                                  Self.slot("1001", .bus, route: "8"), Self.slot("1002", .bus),
                                                  Self.slot("41002", .station)]),
                                       center: Self.belmont)
        #expect(m.close == ["1001", "41001", "1002"])
        Self.expectValid(m)
    }

    @Test func closeIsNearestNotAFarFavorite() {
        // At Belmont with Howard (~9 km) starred: Howard leads the list but can't make the wrist Hyped.
        let m = NearbyBuilder.fromList(Self.list([Self.slot("17380", .bus, meters: 11), Self.slot("41320", .station, meters: 18),
                                                  Self.slot("17379", .bus, meters: 19), Self.slot("41210", .station, meters: 430)],
                                                 favorites: [Self.slot("40900", .station, meters: 9_000)]),
                                       center: Self.belmont)
        #expect(m.trains == ["40900", "41320", "41210"])
        #expect(m.close == ["17380", "41320", "17379"])
        Self.expectValid(m)
    }

    @Test func emptyListIsStillValid() {
        let m = NearbyBuilder.fromList(Self.list([]), center: Self.belmont)
        #expect(m.trains.isEmpty && m.buses.isEmpty && m.close.isEmpty)
        Self.expectValid(m)
    }
}
