import Foundation
import Testing
@testable import CTAKit

// Checkpoint C of iOS milestone 2: the pixel digits and the card data rules.
struct PixelDigitsTests {
    @Test func textGridConcatenatesGlyphsWithOneColumnGaps() throws {
        let grid = try #require(PixelFont.grid("12"))
        #expect(grid.count == PixelFont.glyphHeight)
        #expect(grid.allSatisfy { $0.count == 11 })   // 5 + gap + 5
        let top: [UInt8] = [0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 0]   // "1" top, gap, "2" top
        #expect(grid[0] == top)
        #expect(grid.map { $0[5] }.allSatisfy { $0 == 0 })      // the gap column
    }

    @Test func everyArrivalLabelHasGlyphs() {
        for text in ["Due", "0", "1", "9", "12", "10:05", "12:40", "1234567890"] {
            #expect(PixelFont.grid(text) != nil, "\(text)")
        }
        #expect(PixelFont.grid("Due")?.first?.count == 17)     // 3 glyphs of 5, 2 gaps
        #expect(PixelFont.grid("10:05")?.first?.count == 25)   // 4 glyphs of 5, ":" 1 wide, 4 gaps
    }

    @Test func unknownCharactersHaveNoGrid() {
        #expect(PixelFont.grid("min") == nil)
        #expect(PixelFont.grid("") == nil)
    }

    @Test func glyphsAreAllSevenRowsOfEqualWidth() {
        for (char, rows) in PixelFont.glyphs {
            #expect(rows.count == PixelFont.glyphHeight, "\(char)")
            #expect(Set(rows.map(\.count)).count == 1, "\(char)")
        }
    }
}

struct CardDataTests {
    let now = chicago("2026-11-05T08:00:00")
    func p(_ stop: String, _ route: String, _ dir: String, min: Double) -> Prediction {
        Prediction(stopID: stop, route: route, dir: dir, dest: "", at: now.addingTimeInterval(min * 60), live: true, delayed: false)
    }
    func slot(_ line: Line, _ stop: String, _ fetch: String, _ kind: Kind, _ meters: Double) -> Slot {
        Slot(line: line, stopID: stop, fetchID: fetch, kind: kind, meters: meters)
    }

    @Test func trainSlotTakesItsPlatformAndRouteSoonestFirst() {
        let s = slot(Line(route: "Brn", dir: "Kimball"), "30257", "41320", .station, 100)
        let preds = [p("30257", "Brn", "", min: 9), p("30257", "P", "", min: 2), p("30258", "Brn", "", min: 1),
                     p("30257", "Brn", "", min: 4)]
        #expect(s.predictions(from: preds, now: now).map(\.at) == [now.addingTimeInterval(240), now.addingTimeInterval(540)])
    }

    @Test func busSlotAlsoMatchesDirection() {
        let s = slot(Line(route: "22", dir: "Northbound"), "1001", "1001", .bus, 50)
        let preds = [p("1001", "22", "Northbound", min: 3), p("1001", "22", "Southbound", min: 1), p("1001", "36", "Northbound", min: 2)]
        #expect(s.predictions(from: preds, now: now).count == 1)
    }

    @Test func orderIsDistanceThenSoonestArrival() {
        let a = slot(Line(route: "Red", dir: "Howard"), "30256", "41320", .station, 100)
        let b = slot(Line(route: "Brn", dir: "Kimball"), "30257", "41320", .station, 100)
        let c = slot(Line(route: "22", dir: "Northbound"), "1001", "1001", .bus, 50)
        let d = slot(Line(route: "P", dir: "Linden"), "30257", "41320", .station, 100)   // no arrivals: last at its stop
        let preds = [p("30256", "Red", "", min: 7), p("30257", "Brn", "", min: 2)]
        #expect(CardOrder.sorted([a, b, c, d], predictions: preds, now: now) == [c, b, a, d])
    }

    @Test func departedArrivalsDropAfterAMinute() {
        // A stale card must not show trains that already left as "Due" (review focus 4).
        let s = slot(Line(route: "Red", dir: "Howard"), "30256", "41320", .station, 100)
        let preds = [p("30256", "Red", "", min: -5), p("30256", "Red", "", min: -0.5), p("30256", "Red", "", min: 6)]
        #expect(s.predictions(from: preds, now: now).count == 2)   // 30 s ago still reads "Due"; 5 min ago is gone
        #expect(s.matches(preds[0]))
    }

    @Test func departedArrivalsDontOrderCards() {
        let a = slot(Line(route: "Red", dir: "Howard"), "30256", "41320", .station, 100)
        let b = slot(Line(route: "Brn", dir: "Kimball"), "30257", "41320", .station, 100)
        let preds = [p("30256", "Red", "", min: -10), p("30257", "Brn", "", min: 4)]
        #expect(CardOrder.sorted([a, b], predictions: preds, now: now) == [b, a])
    }

    @Test func slotIDIgnoresDistance() {
        let near = slot(Line(route: "22", dir: "Northbound"), "1001", "1001", .bus, 50)
        let far = slot(Line(route: "22", dir: "Northbound"), "1001", "1001", .bus, 400)
        #expect(near.id == far.id)
        #expect(near.id != slot(Line(route: "22", dir: "Southbound"), "1001", "1001", .bus, 50).id)
    }
}

struct ArrivalBookTests {
    let at = chicago("2026-11-05T08:00:00")
    func p(_ stop: String) -> Prediction {
        Prediction(stopID: stop, route: "22", dir: "Northbound", dest: "", at: at, live: true, delayed: false)
    }

    @Test func busChunkGivesEveryStopItsOwnList() {
        let split = ArrivalBook.split([p("1"), p("3"), p("1")], stops: ["1", "2", "3"])
        #expect(split["1"]?.count == 2)
        #expect(split["2"] == [])          // asked about, none: "No arrivals", not unknown
        #expect(split["3"]?.count == 1)
        #expect(split.count == 3)
    }

    @Test func aFailureKeepsTheLastGoodResult() {
        var book = ArrivalBook()
        book.record(["1": [p("1")]], at: at)
        book.fail(["1", "2"])
        #expect(book.predictions["1"]?.count == 1)
        #expect(book.fetched["1"] == at)
        #expect(!book.unknown("1"))   // has data, just aging
        #expect(book.unknown("2"))    // never loaded and its request failed
        book.record(["2": []], at: at)
        #expect(!book.unknown("2"))
        #expect(!book.unknown("9"))   // never asked yet: still loading, not unknown
    }
}

// iOS redesign task 1: the L lines drawn on the Mapbox map.
struct TrainShapesTests {
    @Test func trainShapesDecodeTheBundledFile() throws {
        let url = URL(filePath: #filePath).deletingLastPathComponent()
            .appending(path: "../../../../data/train_shapes.json").standardized
        let shapes = try TrainShapes.decode(Data(contentsOf: url))
        #expect(shapes.count == 10)
        #expect(Set(shapes.map(\.route)) == Set(LineColors.trains.map(\.route)))
        for s in shapes {
            #expect(s.points.count > 1)
            for p in s.points {
                #expect(p.count == 2)
                #expect((41.6...42.1).contains(p[0]) && (-88.0 ... -87.5).contains(p[1]), "\(s.route) \(p)")
            }
        }
    }
}
