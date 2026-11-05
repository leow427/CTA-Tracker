import CoreGraphics
import Foundation
import Testing
@testable import CTAKit

// Checkpoint B of iOS milestone 2: the CTAKit side of the design kit (app spec 4.6).
struct PixelTests {
    @Test func pixelScale() {
        #expect(PixelScale.scale(art: 32, points: 32) == 1)
        #expect(PixelScale.scale(art: 32, points: 64) == 2)
        #expect(PixelScale.scale(art: 32, points: 120) == 3)   // 3.75 rounds down
        #expect(PixelScale.scale(art: 32, points: 10) == 1)    // never below 1
        #expect(PixelScale.scale(art: 16, points: 128) == 8)
    }

    @Test func pixelFontSnapsAtEveryDynamicTypeSize() {
        // Apple's point sizes for Large Title and Body, xSmall through AX5.
        let largeTitle: [Double] = [31, 32, 33, 34, 36, 38, 40, 44, 48, 52, 56, 60]
        let body: [Double] = [14, 15, 16, 17, 19, 21, 23, 28, 33, 40, 47, 53]
        for points in largeTitle + body {
            let size = PixelFont.snap(points)
            #expect(size.truncatingRemainder(dividingBy: PixelFont.unit) == 0, "\(points) → \(size)")
            #expect(size <= points && size > points - PixelFont.unit)
        }
        #expect(PixelFont.snap(1) == PixelFont.unit)   // never below one pixel
    }

    @Test func petSpritesRenderPaletteWithTransparentSlotZero() throws {
        let image = try #require(PetSprites.image(grid: [[0, 1], [2, 0]], palette: [0x123456, 0xFF0000, 0x00A1DE]))
        #expect(image.width == 2 && image.height == 2)
        let px = rgba(image)
        #expect(px[0] == [0, 0, 0, 0])            // slot 0 is transparent whatever its color
        #expect(px[1] == [0xFF, 0, 0, 0xFF])
        #expect(px[2] == [0, 0xA1, 0xDE, 0xFF])
        #expect(px[3] == [0, 0, 0, 0])
    }

    @Test func petSpritesRejectBadGrids() {
        #expect(PetSprites.image(grid: [], palette: [0]) == nil)
        #expect(PetSprites.image(grid: [[0, 1], [0]], palette: [0, 1]) == nil)   // ragged
        #expect(PetSprites.image(grid: [[0], [0, 1]], palette: [0, 1]) == nil)   // ragged, long row
        #expect(PetSprites.image(grid: [[0, 2]], palette: [0, 1]) == nil)        // index == palette count
        #expect(PetSprites.image(grid: [[0, 5]], palette: [0, 1]) == nil)        // index past the palette
    }

    @Test func gridParsesCharacters() {
        #expect(PetSprites.grid([".K", "K."], chars: ".K") == [[0, 1], [1, 0]])
        #expect(PetSprites.grid([".X"], chars: ".K") == nil)
    }

    func rgba(_ image: CGImage) -> [[UInt8]] {
        var buf = [UInt8](repeating: 0, count: image.width * image.height * 4)
        let ctx = CGContext(data: &buf, width: image.width, height: image.height, bitsPerComponent: 8,
                            bytesPerRow: image.width * 4, space: CGColorSpace(name: CGColorSpace.sRGB)!,
                            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        ctx.draw(image, in: CGRect(x: 0, y: 0, width: image.width, height: image.height))
        return stride(from: 0, to: buf.count, by: 4).map { Array(buf[$0..<$0 + 4]) }
    }
}

struct CardTextTests {
    let now = chicago("2026-11-05T08:00:00")
    func p(_ route: String, _ dest: String, min: Double, live: Bool = true, delayed: Bool = false) -> Prediction {
        Prediction(stopID: "30256", route: route, dir: "", dest: dest, at: now.addingTimeInterval(min * 60 + 10),
                   live: live, delayed: delayed)
    }
    let redN = Line(route: "Red", dir: "Howard")

    @Test func spokenSentence() {
        #expect(CardSpeech.sentence(line: redN, kind: .station, stopName: "Belmont",
                                    next: p("Red", "Howard", min: 3, delayed: true), alert: false, now: now)
                == "Red Line to Howard from Belmont, 3 minutes, live, delayed")
        #expect(CardSpeech.sentence(line: Line(route: "22", dir: "Northbound"), kind: .bus, stopName: "Clark & Lake",
                                    next: p("22", "Howard", min: 1), alert: true, now: now)
                == "Route 22 to Howard from Clark & Lake, 1 minute, live, service alert")
        #expect(CardSpeech.sentence(line: redN, kind: .station, stopName: "Belmont",
                                    next: p("Red", "Howard", min: 0), alert: false, now: now)
                == "Red Line to Howard from Belmont, due, live")
        let sched = Prediction(stopID: "30256", route: "Red", dir: "", dest: "Howard",
                               at: chicago("2026-11-05T10:05:00"), live: false, delayed: false)
        #expect(CardSpeech.sentence(line: redN, kind: .station, stopName: "Belmont", next: sched, alert: false, now: now)
                == "Red Line to Howard from Belmont, at 10:05, scheduled")
    }

    @Test func spokenSentenceWithLaterArrivalsAndAge() {
        let later = [p("Red", "Howard", min: 12),
                     Prediction(stopID: "", route: "Red", dir: "", dest: "", at: chicago("2026-11-05T10:05:00"),
                                live: false, delayed: false)]
        #expect(CardSpeech.sentence(line: redN, kind: .station, stopName: "Belmont", next: p("Red", "Howard", min: 3),
                                    later: later, alert: false, freshness: .aged(minutes: 7), now: now)
                == "Red Line to Howard from Belmont, 3 minutes, live, also 12 minutes and at 10:05, updated 7 minutes ago")
        #expect(CardSpeech.sentence(line: redN, kind: .station, stopName: "Belmont", next: p("Red", "Howard", min: 3),
                                    later: [p("Red", "Howard", min: 1)], alert: false, freshness: .dim(minutes: 3), now: now)
                == "Red Line to Howard from Belmont, 3 minutes, live, also 1 minute, updated 3 minutes ago")
        #expect(CardSpeech.sentence(line: redN, kind: .station, stopName: "Belmont", next: p("Red", "Howard", min: 3),
                                    alert: false, freshness: .fresh, now: now)
                == "Red Line to Howard from Belmont, 3 minutes, live")
        #expect(CardSpeech.sentence(line: redN, kind: .station, stopName: "Belmont", next: nil, alert: false,
                                    freshness: .aged(minutes: 1), now: now)
                == "Red Line to Howard from Belmont, no arrivals, updated 1 minute ago")
    }

    @Test func spokenSentenceWithoutArrivals() {
        #expect(CardSpeech.sentence(line: redN, kind: .station, stopName: "Belmont", next: nil, alert: false, now: now)
                == "Red Line to Howard from Belmont, no arrivals")
        #expect(CardSpeech.sentence(line: redN, kind: .station, stopName: "Belmont", next: nil, alert: false,
                                    now: now, night: true)
                == "Red Line to Howard from Belmont, no service now")
    }

    @Test func lineNames() {
        #expect(CardSpeech.lineName(redN, kind: .station) == "Red Line")
        #expect(CardSpeech.lineName(Line(route: "Brn", dir: "Kimball"), kind: .station) == "Brown Line")
        #expect(CardSpeech.lineName(Line(route: "22", dir: "Northbound"), kind: .bus) == "Route 22")
    }

    @Test func alsoLine() {
        #expect(ArrivalFormat.also([p("Red", "Howard", min: 12), p("Red", "Howard", min: 20)], now: now) == "also 12, 20 min")
        #expect(ArrivalFormat.also([p("Red", "Howard", min: 1)], now: now) == "also 1 min")
        let sched = Prediction(stopID: "", route: "Red", dir: "", dest: "", at: chicago("2026-11-05T10:05:00"),
                               live: false, delayed: false)
        #expect(ArrivalFormat.also([p("Red", "Howard", min: 12), sched], now: now) == "also 12 min, 10:05")
        #expect(ArrivalFormat.also([p("Red", "Howard", min: 0), p("Red", "Howard", min: 12)], now: now) == "also Due, 12 min")
        #expect(ArrivalFormat.also([], now: now) == nil)
    }
}
