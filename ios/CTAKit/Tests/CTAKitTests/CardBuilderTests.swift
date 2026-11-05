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
