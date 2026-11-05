import CoreLocation
import Foundation
import Testing
@testable import CTAKit

// Checkpoint D of iOS milestone 2: stop detail groups and search.
struct DetailGroupsTests {
    let now = chicago("2026-11-05T08:00:00")
    let redN = Line(route: "Red", dir: "Howard"), redS = Line(route: "Red", dir: "95th/Dan Ryan")
    let brnN = Line(route: "Brn", dir: "Kimball"), brnS = Line(route: "Brn", dir: "Loop")
    var belmont: Stop {
        Stop(id: "41320", name: "Belmont", lat: 41.9398, lon: -87.6533, kind: .station, lines: nil, platforms: [
            Platform(id: "30255", lines: [redN]), Platform(id: "30256", lines: [redS]),
            Platform(id: "30257", lines: [brnN]), Platform(id: "30258", lines: [brnS]),
        ])
    }
    func p(_ stop: String, _ route: String, min: Double) -> Prediction {
        Prediction(stopID: stop, route: route, dir: "", dest: "", at: now.addingTimeInterval(min * 60), live: true, delayed: false)
    }

    @Test func tappedGroupComesFirstThenLineThenDirection() {   // 2 lines × 2 directions
        // A stable order (app spec 5.1: by line, then direction), so groups don't jump as trains leave.
        let preds = [p("30255", "Red", min: 9), p("30256", "Red", min: 2), p("30257", "Brn", min: 5)]
        let groups = DetailGroups.groups(stop: belmont, tapped: brnS, predictions: preds, now: now)
        #expect(groups.map(\.line) == [brnS, redS, redN, brnN])   // Red before Brown (CTA line order), dirs A–Z
        #expect(groups.first?.arrivals.isEmpty == true)   // tapped first even with nothing coming
        let later = DetailGroups.groups(stop: belmont, tapped: brnS, predictions: [p("30255", "Red", min: 1)], now: now)
        #expect(later.map(\.line) == groups.map(\.line))
    }

    @Test func atMostFourArrivalsPerGroup() {
        let preds = (1...6).map { p("30255", "Red", min: Double($0)) }
        let groups = DetailGroups.groups(stop: belmont, tapped: redN, predictions: preds, now: now)
        #expect(groups.first?.arrivals.count == 4)
        #expect(groups.first?.arrivals.first?.at == now.addingTimeInterval(60))
    }

    @Test func busStopShowsEveryRouteAtThatStop() {
        let stop = Stop(id: "1001", name: "Clark & Belmont", lat: 41.94, lon: -87.65, kind: .bus, lines: [
            Line(route: "22", dir: "Northbound"), Line(route: "77", dir: "Eastbound")], platforms: nil)
        let groups = DetailGroups.groups(stop: stop, tapped: nil, predictions: [], now: now)
        #expect(groups.map(\.line.route) == ["22", "77"])
        #expect(groups.allSatisfy { $0.stopID == "1001" && $0.kind == .bus })
    }
}

struct StopSearchTests {
    let center = CLLocationCoordinate2D(latitude: 41.9398, longitude: -87.6533)
    var search: StopSearch.Index { StopSearch.Index(index) }
    var index: StopIndex {
        StopIndex(stops: [
            Stop(id: "41320", name: "Belmont", lat: 41.9398, lon: -87.6533, kind: .station, lines: nil,
                 platforms: [Platform(id: "30256", lines: [Line(route: "Red", dir: "Howard")])]),
            Stop(id: "41220", name: "Fullerton", lat: 41.9253, lon: -87.6528, kind: .station, lines: nil,
                 platforms: [Platform(id: "30234", lines: [Line(route: "Brn", dir: "Kimball")])]),
            Stop(id: "1001", name: "Clark & Belmont", lat: 41.9399, lon: -87.6480, kind: .bus,
                 lines: [Line(route: "22", dir: "Northbound")], platforms: nil),
            Stop(id: "1002", name: "Montrose & Clark", lat: 41.9616, lon: -87.6652, kind: .bus,
                 lines: [Line(route: "122", dir: "Northbound")], platforms: nil),
        ])
    }

    @Test func namesMatchCaseInsensitivelyNearestFirst() {
        let r = StopSearch.results("BELMONT", center: center, favorites: [], index: search)
        #expect(r.stops.map(\.stop.id) == ["41320", "1001"])
    }

    @Test func stationsRankBeforeBusStopsNamedAfterThem() {
        let index = StopIndex(stops: self.index.stops + [
            Stop(id: "1003", name: "Fullerton Red/Brown/Purple Line Station", lat: 41.9399, lon: -87.6534, kind: .bus,
                 lines: [Line(route: "74", dir: "Eastbound")], platforms: nil)])
        let r = StopSearch.results("fuller", center: center, favorites: [], index: StopSearch.Index(index))
        #expect(r.stops.map(\.stop.id) == ["41220", "1003"])   // the station first, though the bus stop is nearer
    }

    @Test func routeNumbersMatchExactly() {
        #expect(StopSearch.results("22", center: center, favorites: [], index: search).stops.map(\.stop.id) == ["1001"])
        #expect(StopSearch.results("122", center: center, favorites: [], index: search).stops.map(\.stop.id) == ["1002"])
    }

    @Test func trainLinesMatchByNameOrID() {
        #expect(StopSearch.results("brown", center: center, favorites: [], index: search).stops.map(\.stop.id) == ["41220"])
        #expect(StopSearch.results("brn", center: center, favorites: [], index: search).stops.map(\.stop.id) == ["41220"])
        #expect(StopSearch.results("red line", center: center, favorites: [], index: search).stops.map(\.stop.id) == ["41320"])
    }

    @Test func favoritesComeFirstAndEmptyQueryShowsThem() {
        let fav = Slot(line: Line(route: "22", dir: "Northbound"), stopID: "1001", fetchID: "1001", kind: .bus, meters: 400)
        #expect(StopSearch.results("", center: center, favorites: [fav], index: search).favorites == [fav])
        #expect(StopSearch.results("", center: center, favorites: [fav], index: search).stops.isEmpty)
        #expect(StopSearch.results("clark", center: center, favorites: [fav], index: search).favorites == [fav])
        #expect(StopSearch.results("fullerton", center: center, favorites: [fav], index: search).favorites.isEmpty)
    }
}

extension StopSearchTests {
    @Test func accentsFold() {
        let index = StopIndex(stops: [Stop(id: "9", name: "Paseo Boricúa", lat: 41.90, lon: -87.70, kind: .bus,
                                           lines: [Line(route: "70", dir: "Eastbound")], platforms: nil)])
        #expect(StopSearch.results("boricua", center: center, favorites: [], index: StopSearch.Index(index)).stops.count == 1)
    }

    @Test func trainNamesMatchFromTheStartNotInside() {
        #expect(StopSearch.results("ed", center: center, favorites: [], index: search).stops.isEmpty)   // not "Red"
    }

    @Test func favoritesMatchByRouteNumber() {
        let fav = Slot(line: Line(route: "22", dir: "Northbound"), stopID: "1001", fetchID: "1001", kind: .bus, meters: 400)
        #expect(StopSearch.results("22", center: center, favorites: [fav], index: search).favorites == [fav])
    }

    @Test func theSwitchFiltersSearchToo() {
        let fav = Slot(line: Line(route: "22", dir: "Northbound"), stopID: "1001", fetchID: "1001", kind: .bus, meters: 400)
        let trains = StopSearch.results("belmont", center: center, favorites: [fav], kinds: [.station], index: search)
        #expect(trains.stops.map(\.stop.id) == ["41320"])
        #expect(StopSearch.results("clark", center: center, favorites: [fav], kinds: [.station], index: search).favorites.isEmpty)
        #expect(StopSearch.results("clark", center: center, favorites: [fav], kinds: [.bus], index: search).favorites == [fav])
    }

    @Test func atMostThirtyStops() {
        let many = (0..<40).map { Stop(id: "s\($0)", name: "Ashland & \($0)th", lat: 41.9 + Double($0) / 1000, lon: -87.66,
                                       kind: .bus, lines: [Line(route: "9", dir: "Northbound")], platforms: nil) }
        let r = StopSearch.results("ashland", center: center, favorites: [], index: StopSearch.Index(StopIndex(stops: many)))
        #expect(r.stops.count == StopSearch.limit)
    }
}

struct FavoritesTests {
    @Test func allKnownFavoritesResolveButOnlyThreeShow() {
        // App spec 2: up to 3 in the ★ section, the rest under ★ in search.
        let lines = (1...5).map { Line(route: "\($0)", dir: "Northbound") }
        let stop = Stop(id: "b", name: "B", lat: 41.88, lon: -87.63, kind: .bus, lines: lines, platforms: nil)
        let favs = lines.map { Favorite(stopID: "b", line: $0) } + [Favorite(stopID: "gone", line: lines[0])]
        let index = StopIndex(stops: [stop])
        let center = CLLocationCoordinate2D(latitude: 41.88, longitude: -87.63)
        #expect(CardBuilder.resolved(favorites: favs, center: center, index: index).count == 5)   // unknown dropped
        #expect(CardBuilder.slots(center: center, filter: .all, favorites: favs, index: index).favorites.count == 3)
    }
}

struct AlertTextTests {
    @Test func htmlBecomesPlainText() {
        let html = "<p>Trains run&nbsp;every 15 min.</p><p>Use <b>Red</b> &amp; Brown&#39;s stops.<br/>Thanks</p>"
        #expect(AlertsWeather.plainText(html) == "Trains run every 15 min.\nUse Red & Brown's stops.\nThanks")
        #expect(AlertsWeather.plainText("  plain  ") == "plain")
        // Live Customer Alerts text (2026-11-05) uses typographic and numeric entities.
        #expect(AlertsWeather.plainText("the Loop &lsquo;L&rsquo; lines &mdash; and &#8220;more&#x201D; &hellip;")
                == "the Loop \u{2018}L\u{2019} lines \u{2014} and \u{201C}more\u{201D} \u{2026}")
    }
}
