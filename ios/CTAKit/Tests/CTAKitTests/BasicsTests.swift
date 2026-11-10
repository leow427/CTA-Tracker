import Foundation
import Testing
@testable import CTAKit

// Mirrors firmware test_basics (plan 1, task 2).
struct BasicsTests {
    @Test func lineColors() {
        #expect(LineColors.rgb("Red") == 0xC60C30)
        #expect(LineColors.rgb("red") == 0xC60C30)   // ttpositions uses lowercase
        #expect(LineColors.rgb("Blue") == 0x2F7BFF)
        #expect(LineColors.rgb("Brn") == 0x62361B)
        #expect(LineColors.rgb("G") == 0x009B3A)
        #expect(LineColors.rgb("Org") == 0xF9461C)
        #expect(LineColors.rgb("Pink") == 0xE27EA6)
        #expect(LineColors.rgb("P") == 0x522398)
        #expect(LineColors.rgb("Y") == 0xF9E300)
        #expect(LineColors.rgb("22") == 0x565A5C)   // every bus route
    }

    // Navy restyle: line colors as text on the navy panel and map (Transit-style rows).
    @Test func onNavyReadsOnNavy() {
        for rgb in LineColors.trains.map(\.rgb) + [LineColors.busAccent] {
            let text = LineColors.onNavy(rgb)
            #expect(LineColors.contrast(text, LineColors.navy) >= 4.5, "\(String(rgb, radix: 16))")
        }
        #expect(LineColors.onNavy(0xF9E300) == 0xF9E300)   // already reads: unchanged
        #expect(LineColors.onNavy(0x62361B) != 0x62361B)   // Brown is lifted toward white
        for rgb in LineColors.trains.map(\.rgb) {   // large text only needs 3:1, so it keeps truer color
            let big = LineColors.onNavy(rgb, minimum: 3)
            #expect(LineColors.contrast(big, LineColors.navy) >= 3, "\(String(rgb, radix: 16))")
            #expect(LineColors.contrast(big, LineColors.navy) <= LineColors.contrast(LineColors.onNavy(rgb), LineColors.navy))
        }
        #expect(LineColors.accent("66") == LineColors.busAccent)
        #expect(LineColors.accent("Red") == 0xC60C30)
    }

    /// Each line's card (redesign revision 2): a darker version of its color, with a lighter version as the text.
    @Test func lineCardsAreDarkerWithLighterText() {
        for rgb in LineColors.trains.map(\.rgb) + [LineColors.busAccent] {
            let (card, text) = (LineColors.card(rgb), LineColors.cardText(rgb))
            let hex = String(rgb, radix: 16)
            #expect(LineColors.luminance(card) < LineColors.luminance(rgb), "\(hex) card is darker")
            #expect(LineColors.luminance(text) > LineColors.luminance(rgb), "\(hex) text is lighter")
            #expect(LineColors.contrast(text, card) >= 4.5, "\(hex) text reads on its card")
            // The gradient's lighter top is still darker than the line, and the text reads there too.
            let top = LineColors.cardTop(rgb)
            #expect(LineColors.luminance(card) < LineColors.luminance(top), "\(hex) top is lighter than the bottom")
            #expect(LineColors.luminance(top) < LineColors.luminance(rgb), "\(hex) top is darker than the line")
            #expect(LineColors.contrast(text, top) >= 4.5, "\(hex) text reads at the gradient's top")
            #expect(LineColors.contrast(card, LineColors.navy) >= 1.4, "\(hex) card stands off the navy panel")
        }
    }

    @Test func everyLineTextColorPassesContrast() {
        let colors = LineColors.trains.map(\.rgb) + [LineColors.bus]
        #expect(colors.count == 9)
        for rgb in colors {
            #expect(LineColors.contrast(LineColors.textColor(on: rgb), rgb) >= 4.5, "\(String(rgb, radix: 16))")
        }
        #expect(LineColors.dimmed(0x000000, dark: false) == 0x727275)   // halfway to the light background gray
        #expect(LineColors.dimmed(0xFFFFFF, dark: true) == 0x9C9C9D)    // halfway to the dark background gray
        // Stale cards dim their fill toward the background, never away from it, and their text still passes (app spec 8).
        for dark in [false, true] {
            let background: UInt32 = dark ? 0x000000 : 0xFFFFFF
            for rgb in colors {
                let dim = LineColors.dimmed(rgb, dark: dark)
                let name = "\(dark ? "dark" : "light") \(String(rgb, radix: 16))"
                #expect(dim != rgb, "\(name)")
                #expect(LineColors.contrast(dim, background) <= LineColors.contrast(rgb, background), "\(name)")
                #expect(LineColors.contrast(LineColors.textColor(on: dim), dim) >= 4.5, "\(name)")
            }
        }
        #expect(LineColors.textColor(on: LineColors.rgb("Y")) == 0x000000)
        #expect(LineColors.textColor(on: LineColors.rgb("Red")) == 0xFFFFFF)
    }

    @Test func ctaTimeDifferences() throws {
        func diff(_ a: String, _ b: String) throws -> TimeInterval {
            try #require(CTATime.naive(b)) - (try #require(CTATime.naive(a)))
        }
        #expect(try diff("2026-11-05T08:00:00", "2026-11-05T08:03:00") == 180)
        #expect(try diff("20261105 08:00", "20261105 08:04:30") == 270)
        #expect(try diff("2026-11-05T23:58:30", "2026-11-06T00:03:30") == 300)   // midnight
        #expect(try diff("2026-12-31T23:59:00", "2027-01-01T00:01:00") == 120)   // new year
        #expect(try diff("2026-11-01T01:58:00", "2026-11-01T01:59:30") == 90)   // the repeated hour
        #expect(try diff("2026-03-08T01:59:00", "2026-03-08T03:01:00") == 3_720) // naive: no DST jump
        #expect(CTATime.naive("garbage") == nil)
        #expect(CTATime.naive("2026-13-05T08:00:00") == nil)
        #expect(CTATime.naive("2026-11-31T08:00:00") == nil)
    }

    @Test func nightHours() {
        #expect(CTATime.inNightHours(minuteOfDay: 60, start: 60, end: 300))     // 1:00 starts night
        #expect(!CTATime.inNightHours(minuteOfDay: 300, start: 60, end: 300))   // 5:00 ends it (exclusive)
        #expect(!CTATime.inNightHours(minuteOfDay: 59, start: 60, end: 300))
        #expect(CTATime.inNightHours(minuteOfDay: 23 * 60 + 30, start: 23 * 60, end: 6 * 60))   // wraps midnight
        #expect(!CTATime.inNightHours(minuteOfDay: 7 * 60, start: 23 * 60, end: 6 * 60))
        #expect(!CTATime.inNightHours(minuteOfDay: 100, start: 120, end: 120))  // start == end means never
    }
}
