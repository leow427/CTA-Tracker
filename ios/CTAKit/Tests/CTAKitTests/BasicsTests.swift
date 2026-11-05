import Foundation
import Testing
@testable import CTAKit

// Mirrors firmware test_basics (plan 1, task 2).
struct BasicsTests {
    @Test func lineColors() {
        #expect(LineColors.rgb("Red") == 0xC60C30)
        #expect(LineColors.rgb("red") == 0xC60C30)   // ttpositions uses lowercase
        #expect(LineColors.rgb("Blue") == 0x00A1DE)
        #expect(LineColors.rgb("Brn") == 0x62361B)
        #expect(LineColors.rgb("G") == 0x009B3A)
        #expect(LineColors.rgb("Org") == 0xF9461C)
        #expect(LineColors.rgb("Pink") == 0xE27EA6)
        #expect(LineColors.rgb("P") == 0x522398)
        #expect(LineColors.rgb("Y") == 0xF9E300)
        #expect(LineColors.rgb("22") == 0x565A5C)   // every bus route
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
