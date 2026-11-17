import CoreLocation
import CTAKit
import Foundation
import Testing
@testable import CTATracker

// Places in search (Leo 2026-11-17): held to the CTA's area, after the stops, and a picked place lists its stops.
struct PlaceSearchTests {
    static let index: StopIndex = {
        let url = Bundle.main.url(forResource: "stops", withExtension: "json")!
        return try! StopIndex(data: Data(contentsOf: url))
    }()

    @Test func theRegionHoldsEveryStop() {
        let outside = Self.index.stops.filter { !PlaceSearch.contains(CLLocationCoordinate2D(latitude: $0.lat, longitude: $0.lon)) }
        #expect(outside.isEmpty, "\(outside.prefix(3).map(\.name))")
    }

    @Test(arguments: [(41.9484, -87.6553), (41.8796, -87.6237), (41.9786, -87.9048), (41.7868, -87.7522)])
    func chicagoPlacesAreIn(lat: Double, lon: Double) {
        #expect(PlaceSearch.contains(CLLocationCoordinate2D(latitude: lat, longitude: lon)))
    }

    @Test(arguments: [(43.0389, -87.9065), (41.5934, -87.3464), (40.7128, -74.0060)])   // Milwaukee, Gary, New York
    func farPlacesAreOut(lat: Double, lon: Double) {
        #expect(!PlaceSearch.contains(CLLocationCoordinate2D(latitude: lat, longitude: lon)))
    }

    @Test func placesAreAShortSectionAfterStops() {
        #expect(PlaceSearch.limit(stopHits: 4) == 3)
        #expect(PlaceSearch.limit(stopHits: 0) > 3)
    }

    @Test func placesShowOnlyWhileTheyAnswerWhatsTyped() {
        #expect(PlaceSearch.isCurrent(answered: "wrigley", query: "wrigley"))
        #expect(PlaceSearch.isCurrent(answered: "wrigley", query: "Wrigley f"))   // typing on
        #expect(!PlaceSearch.isCurrent(answered: "wrigley", query: "wrigle"))     // a deleted letter
        #expect(!PlaceSearch.isCurrent(answered: "wrigley", query: "art"))        // a new query
        #expect(!PlaceSearch.isCurrent(answered: "wrigley", query: ""))           // cleared
        #expect(!PlaceSearch.isCurrent(answered: nil, query: "wrigley"))
    }

    @Test func pickingTheSamePlaceAgainIsANewPick() {
        #expect(Place(name: "Wrigley Field", lat: 41.9484, lon: -87.6553) != Place(name: "Wrigley Field", lat: 41.9484, lon: -87.6553))
    }

    @Test func wrigleyFieldListsAddisonFirst() {
        let wrigley = CLLocationCoordinate2D(latitude: 41.9484, longitude: -87.6553)
        let nearby = CardBuilder.slots(center: wrigley, filter: .trains, favorites: [], index: Self.index).nearby
        #expect(Self.index.stop(id: nearby.first?.fetchID ?? "")?.name == "Addison")
        #expect(nearby.first?.line.route == "Red")
        #expect(nearby.map(\.meters) == nearby.map(\.meters).sorted())
    }
}
