import CoreLocation
import Foundation
import Testing
@testable import CTAKit

// Checkpoint A of iOS milestone 2: StopIndex and the card rules (app spec 4.2).
struct CardBuilderTests {
    static let center = CLLocationCoordinate2D(latitude: 41.8819, longitude: -87.6278)

    /// A point `meters` due north of the center (exact for haversine along a meridian).
    static func north(_ meters: Double) -> (Double, Double) {
        (center.latitude + meters / (StopIndex.earthRadius * .pi / 180), center.longitude)
    }

    static func bus(_ id: String, _ meters: Double, _ lines: [Line]) -> Stop {
        let (lat, lon) = north(meters)
        return Stop(id: id, name: "Stop \(id)", lat: lat, lon: lon, kind: .bus, lines: lines, platforms: nil)
    }

    static func station(_ id: String, _ meters: Double, _ platforms: [Platform]) -> Stop {
        let (lat, lon) = north(meters)
        return Stop(id: id, name: "Station \(id)", lat: lat, lon: lon, kind: .station, lines: nil, platforms: platforms)
    }

    static let n22 = Line(route: "22", dir: "Northbound")
    static let s22 = Line(route: "22", dir: "Southbound")
    static let n36 = Line(route: "36", dir: "Northbound")
    static let redN = Line(route: "Red", dir: "Howard")
    static let redS = Line(route: "Red", dir: "95th/Dan Ryan")
    static let blueN = Line(route: "Blue", dir: "O'Hare")
    static let blueS = Line(route: "Blue", dir: "Forest Park")

    func slots(_ stops: [Stop], filter: KindFilter = .all, favorites: [Favorite] = []) -> SlotList {
        CardBuilder.slots(center: Self.center, filter: filter, favorites: favorites, index: StopIndex(stops: stops))
    }

    @Test func sixStopsOnOneLineGiveOneSlotAtTheNearest() {
        let stops = [600, 100, 500, 200, 400, 300].map { Self.bus("b\($0)", Double($0), [Self.n22]) }
        let list = slots(stops)
        #expect(list.nearby.map(\.stopID) == ["b100"])
        #expect(list.nearby.first?.line == Self.n22)
        #expect(abs((list.nearby.first?.meters ?? 0) - 100) < 0.01)
    }

    @Test func multiLineStationGivesOneSlotPerLineAndDirection() {   // review focus 1
        let station = Self.station("40380", 100, [
            Platform(id: "30074", lines: [Self.redN, Self.blueN]),
            Platform(id: "30075", lines: [Self.redS, Self.blueS]),
        ])
        let list = slots([station])
        #expect(Set(list.nearby.map(\.line)) == [Self.redN, Self.redS, Self.blueN, Self.blueS])
        #expect(list.nearby.count == 4)
        let redS = list.nearby.first { $0.line == Self.redS }
        #expect(redS?.stopID == "30075")
        #expect(redS?.fetchID == "40380")
        #expect(redS?.kind == .station)
    }

    @Test func eightHundredMeterCutoff() {
        let list = slots([Self.bus("near", 790, [Self.n22]), Self.bus("far", 810, [Self.n36])])
        #expect(list.nearby.map(\.stopID) == ["near"])
    }

    @Test func busSlotsFetchTheirOwnStop() {
        let list = slots([Self.bus("1001", 50, [Self.n22, Self.n36])])
        #expect(list.nearby.map(\.fetchID) == ["1001", "1001"])
        #expect(list.nearby.allSatisfy { $0.kind == .bus })
    }

    @Test func nearbyIsSortedByDistance() {
        let list = slots([Self.bus("b3", 300, [Self.n36]), Self.bus("b1", 100, [Self.n22])])
        #expect(list.nearby.map(\.stopID) == ["b1", "b3"])
    }

    @Test func trainsFallBackToFiveNearestStations() {
        let stations = (1...7).map { Self.station("s\($0)", Double($0) * 1000,
                                                  [Platform(id: "p\($0)", lines: [Line(route: "G", dir: "d\($0)")])]) }
        let list = slots(stations + [Self.bus("b", 100, [Self.n22])], filter: .trains)
        #expect(list.nearby.map(\.fetchID) == ["s1", "s2", "s3", "s4", "s5"])
        #expect(!list.farFromChicago)
    }

    @Test func busesFilterHidesStations() {
        let list = slots([Self.station("s", 50, [Platform(id: "p", lines: [Self.redN])]),
                          Self.bus("b", 100, [Self.n22])], filter: .buses)
        #expect(list.nearby.map(\.stopID) == ["b"])
    }

    @Test func allFallsBackOnlyWhenNoStopOfEitherKindIsNear() {
        let farStations = (1...6).map { Self.station("s\($0)", Double($0) * 1000,
                                                     [Platform(id: "p\($0)", lines: [Line(route: "G", dir: "d\($0)")])]) }
        // A bus stop is near, so All shows just it, without the station fallback.
        let near = slots(farStations + [Self.bus("b", 100, [Self.n22])])
        #expect(near.nearby.map(\.stopID) == ["b"])
        // Nothing near: the 5 nearest stops of either kind.
        let none = slots(farStations + [Self.bus("b", 2500, [Self.n22])])
        #expect(none.nearby.map(\.fetchID) == ["s1", "s2", "b", "s3", "s4"])
    }

    @Test func farFromChicagoEmptiesNearbyButKeepsFavorites() {
        let stops = [Self.bus("b1", 30_500, [Self.n22]), Self.bus("b2", 31_000, [Self.n36])]
        let list = slots(stops, favorites: [Favorite(stopID: "b2", line: Self.n36)])
        #expect(list.farFromChicago)
        #expect(list.nearby.isEmpty)
        #expect(list.favorites.map(\.stopID) == ["b2"])
        #expect(!slots([Self.bus("b1", 29_500, [Self.n22])]).farFromChicago)
    }

    @Test func farFromChicagoLooksAtEveryKindWhateverTheFilter() {
        // Under Trains, the only station is 40 km away but a bus stop is close: not far from Chicago.
        let list = slots([Self.station("s", 40_000, [Platform(id: "p", lines: [Self.redN])]),
                          Self.bus("b", 100, [Self.n22])], filter: .trains)
        #expect(!list.farFromChicago)
        #expect(list.nearby.map(\.fetchID) == ["s"])
    }

    @Test func favoriteHidesTheNearbySlotWithTheSameLine() {
        let stops = [Self.bus("b1", 100, [Self.n22, Self.n36]), Self.bus("fav", 300, [Self.n22])]
        let list = slots(stops, favorites: [Favorite(stopID: "fav", line: Self.n22)])
        #expect(list.favorites.map(\.stopID) == ["fav"])
        #expect(list.favorites.first?.line == Self.n22)
        #expect(list.nearby.map(\.line) == [Self.n36])
    }

    @Test func farFavoriteHidesNothing() {
        let stops = [Self.bus("b1", 100, [Self.n22]), Self.bus("fav", 5000, [Self.n22])]
        let list = slots(stops, favorites: [Favorite(stopID: "fav", line: Self.n22)])
        #expect(list.favorites.map(\.stopID) == ["fav"])
        #expect(list.nearby.map(\.stopID) == ["b1"])
    }

    @Test func trainFavoriteKeysOnItsPlatform() {
        let station = Self.station("40380", 100, [Platform(id: "30074", lines: [Self.redN]),
                                                  Platform(id: "30075", lines: [Self.redS])])
        let list = slots([station], favorites: [Favorite(stopID: "30075", line: Self.redS)])
        let fav = list.favorites.first
        #expect(fav?.stopID == "30075")
        #expect(fav?.fetchID == "40380")
        #expect(fav?.kind == .station)
        #expect(list.nearby.map(\.line) == [Self.redN])
    }

    @Test func unknownFavoritesAreDropped() {   // review focus 2
        let stops = [Self.bus("b1", 100, [Self.n22])]
        let list = slots(stops, favorites: [Favorite(stopID: "gone", line: Self.n36),
                                            Favorite(stopID: "b1", line: Self.s22),   // stop exists, line no longer serves it
                                            Favorite(stopID: "b1", line: Self.n22)])
        #expect(list.favorites.map(\.line) == [Self.n22])
    }

    @Test func atMostThreeFavorites() {
        let lines = (1...5).map { Line(route: "\($0)", dir: "Northbound") }
        let stops = [Self.bus("b", 100, lines)]
        let list = slots(stops, favorites: lines.map { Favorite(stopID: "b", line: $0) })
        #expect(list.favorites.map(\.line) == Array(lines.prefix(3)))
        #expect(list.nearby.map(\.line) == Array(lines.suffix(2)))
    }

    @Test func kindFilterAppliesToFavorites() {
        let stops = [Self.bus("b", 100, [Self.n22]), Self.station("s", 200, [Platform(id: "p", lines: [Self.redN])])]
        let favs = [Favorite(stopID: "b", line: Self.n22), Favorite(stopID: "p", line: Self.redN)]
        #expect(slots(stops, filter: .trains, favorites: favs).favorites.map(\.stopID) == ["p"])
        #expect(slots(stops, filter: .buses, favorites: favs).favorites.map(\.stopID) == ["b"])
    }

    @Test func emptyIndexIsFarFromChicago() {
        let list = slots([])
        #expect(list.farFromChicago)
        #expect(list.nearby.isEmpty)
    }
}

struct StopIndexTests {
    @Test func decodesTheStopsJSONShape() throws {
        // The shape plan 1 task 10 writes to data/stops.json.
        let json = """
        [{"id":"1001","name":"Clark & Lake","lat":41.8857,"lon":-87.6309,"kind":"bus",
          "lines":[{"route":"22","dir":"Northbound"}]},
         {"id":"40380","name":"Clark/Lake","lat":41.885737,"lon":-87.630886,"kind":"station",
          "platforms":[{"id":"30074","lines":[{"route":"Blue","dir":"O'Hare"},{"route":"Brn","dir":"Kimball"}]}]}]
        """
        let index = try StopIndex(data: Data(json.utf8))
        #expect(index.stops.count == 2)
        #expect(index.stop(id: "1001")?.lines == [Line(route: "22", dir: "Northbound")])
        #expect(index.stop(id: "40380")?.platforms?.first?.lines.count == 2)
        #expect(index.station(ofPlatform: "30074")?.id == "40380")
        #expect(index.station(ofPlatform: "1001") == nil)
    }

    @Test func haversine() {
        // State & Madison to Clark/Lake is about 0.5 km.
        let a = CLLocationCoordinate2D(latitude: 41.8819, longitude: -87.6278)
        let b = CLLocationCoordinate2D(latitude: 41.885737, longitude: -87.630886)
        #expect(abs(StopIndex.meters(a, b) - 497) < 5)
        #expect(StopIndex.meters(a, a) == 0)
    }
}

/// `CardBuilder.slots` skips the full sort of every stop (it runs after every pan); it must still build exactly
/// what sorting everything did.
struct CardBuilderSpeedupTests {
    /// The rules as first written: every stop sorted by distance, then filtered.
    static func reference(center: CLLocationCoordinate2D, filter: KindFilter, favorites: [Favorite],
                          index: StopIndex) -> SlotList {
        let kinds = filter.kinds
        let favs = CardBuilder.resolved(favorites: favorites, center: center, index: index)
            .filter { kinds.contains($0.kind) }.prefix(Config.maxFavorites)
        let all = index.byDistance(from: center)
        guard let nearestAny = all.first?.meters, nearestAny <= Config.farFromChicagoM else {
            return SlotList(favorites: Array(favs), nearby: [], farFromChicago: true)
        }
        let sorted = all.filter { kinds.contains($0.stop.kind) }
        let inRadius = sorted.prefix { $0.meters <= Config.cardRadiusM }
        let shown = inRadius.isEmpty ? sorted.prefix(Config.fallbackStops) : inRadius
        let shownIDs = Set(shown.map(\.stop.id))
        var seen = Set(favs.filter { shownIDs.contains($0.fetchID) }.map(\.line))
        let nearby = shown.flatMap { CardBuilder.slots(of: $0.stop, meters: $0.meters) }.filter { seen.insert($0.line).inserted }
        return SlotList(favorites: Array(favs), nearby: nearby, farFromChicago: false)
    }

    /// The app's real stops (`data/stops.json`, ~11k).
    static let index: StopIndex = {
        let url = URL(filePath: #filePath).deletingLastPathComponent().appending(path: "../../../../data/stops.json").standardized
        return try! StopIndex(data: Data(contentsOf: url))
    }()

    static func expectSame(_ center: CLLocationCoordinate2D, index: StopIndex = index, favorites: [Favorite] = []) {
        for filter in [KindFilter.all, .trains, .buses] {
            let fast = CardBuilder.slots(center: center, filter: filter, favorites: favorites, index: index)
            let slow = reference(center: center, filter: filter, favorites: favorites, index: index)
            #expect(fast == slow, "\(center.latitude),\(center.longitude) \(filter)")
        }
    }

    @Test func sameAsSortingEverythingAcrossChicago() {
        #expect(Self.index.stops.count > 10_000)
        // The Loop, Belmont, O'Hare, the far South Side, the lake (sparse: the fallback), and a grid over the city.
        let places = [(41.8819, -87.6278), (41.9398, -87.6533), (41.9779, -87.9045), (41.6600, -87.6000),
                      (41.9000, -87.5000), (42.0700, -87.6800), (41.7400, -87.5300)]
        for (lat, lon) in places { Self.expectSame(CLLocationCoordinate2D(latitude: lat, longitude: lon)) }
        for lat in stride(from: 41.60, through: 42.10, by: 0.05) {
            for lon in stride(from: -87.95, through: -87.45, by: 0.05) {
                Self.expectSame(CLLocationCoordinate2D(latitude: lat, longitude: lon))
            }
        }
    }

    @Test func sameWithFavorites() {
        let favorites = [Favorite(stopID: "30074", line: Line(route: "Blue", dir: "O'Hare")),
                         Favorite(stopID: "1001", line: Line(route: "22", dir: "Northbound"))]
        Self.expectSame(CLLocationCoordinate2D(latitude: 41.8857, longitude: -87.6309), favorites: favorites)
    }

    @Test func sameFarFromChicagoAndAtItsEdge() {
        Self.expectSame(CLLocationCoordinate2D(latitude: 40.7128, longitude: -74.0060))   // New York
        // Just inside and just outside 30 km from the only stop.
        let stop = CardBuilderTests.bus("only", 0, [CardBuilderTests.n22])
        let index = StopIndex(stops: [stop])
        for meters in [29_999.0, 30_000, 30_001] {
            let (lat, lon) = CardBuilderTests.north(meters)
            Self.expectSame(CLLocationCoordinate2D(latitude: lat, longitude: lon), index: index)
        }
        Self.expectSame(CardBuilderTests.center, index: StopIndex(stops: []))
    }

    @Test func sameAtTheRadiusEdgeWithTies() {
        // Stops right at 800 m, tied distances broken by ID, and a station among buses, from either side of the edge.
        let stops = [CardBuilderTests.bus("b2", 800, [CardBuilderTests.n22]), CardBuilderTests.bus("b1", 800, [CardBuilderTests.n36]),
                     CardBuilderTests.bus("b3", 800.5, [CardBuilderTests.s22]), CardBuilderTests.bus("b4", 799.5, [CardBuilderTests.n22]),
                     CardBuilderTests.station("s1", 800, [Platform(id: "p1", lines: [CardBuilderTests.redN])]),
                     CardBuilderTests.station("s2", 1_500, [Platform(id: "p2", lines: [CardBuilderTests.blueN])])]
        let index = StopIndex(stops: stops)
        for shift in [0.0, 0.4, 1, 400, 900, 2_000, 5_000] {
            let (lat, lon) = CardBuilderTests.north(-shift)
            Self.expectSame(CLLocationCoordinate2D(latitude: lat, longitude: lon), index: index)
        }
    }

    @Test func aStopExactlyAtTheRadiusIsShown() throws {
        // A stop haversine puts exactly 800 m out: `<=`, not `<`, must list it. Near the equator a latitude's ulp is a
        // fraction of a picometer, so stepping by ulps lands on 800 exactly (around Chicago it moves ~1 nm a step).
        let center = CLLocationCoordinate2D(latitude: 0, longitude: 0)
        func meters(_ lat: Double) -> Double { StopIndex.meters(center, CLLocationCoordinate2D(latitude: lat, longitude: 0)) }
        var lat = Config.cardRadiusM / (StopIndex.earthRadius * .pi / 180)
        for _ in 0..<100_000 where meters(lat) != Config.cardRadiusM {
            lat = meters(lat) < Config.cardRadiusM ? lat.nextUp : lat.nextDown
        }
        try #require(meters(lat) == Config.cardRadiusM)
        let edge = Stop(id: "edge", name: "Edge", lat: lat, lon: 0, kind: .bus, lines: [CardBuilderTests.n22], platforms: nil)
        let far = Stop(id: "far", name: "Far", lat: 2 * lat, lon: 0, kind: .bus, lines: [CardBuilderTests.n36], platforms: nil)
        let index = StopIndex(stops: [edge, far])
        Self.expectSame(center, index: index)
        #expect(CardBuilder.slots(center: center, filter: .all, favorites: [], index: index).nearby.map(\.stopID) == ["edge"])
    }

    @Test func fallbackTakesTheFiveNearestInOrder() {
        // Nothing within 800 m: the 5 nearest, ties by ID, out of order in the index.
        let stops = [1_900, 1_200, 1_500, 1_200, 3_000, 1_100, 2_500, 1_000].enumerated().map { n, m in
            CardBuilderTests.bus("b\(9 - n)", Double(m), [Line(route: "\(n)", dir: "Northbound")])
        }
        Self.expectSame(CardBuilderTests.center, index: StopIndex(stops: stops))
        let list = CardBuilder.slots(center: CardBuilderTests.center, filter: .all, favorites: [], index: StopIndex(stops: stops))
        #expect(list.nearby.map(\.stopID) == ["b2", "b4", "b6", "b8", "b7"])
    }
}
