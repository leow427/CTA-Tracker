import Foundation
import Testing
@testable import CTAKit

// iOS redesign task 2: the map's light preset follows the sun (redesign spec 6).
struct SunTimesTests {
    static let loop = (lat: 41.8819, lon: -87.6278)

    static func utc(_ s: String) -> Date {
        let f = DateFormatter()
        f.locale = Locale(identifier: "en_US_POSIX")
        f.timeZone = TimeZone(identifier: "UTC")
        f.dateFormat = "yyyy-MM-dd HH:mm"
        return f.date(from: s)!
    }

    static func near(_ a: Date, _ b: Date, minutes: Double = 3) -> Bool { abs(a.timeIntervalSince(b)) <= minutes * 60 }

    @Test func chicagoSummerSolstice() throws {
        let t = try #require(SunTimes.riseSet(on: chicago("2026-06-21T12:00:00"), lat: Self.loop.lat, lon: Self.loop.lon))
        #expect(Self.near(t.rise, Self.utc("2026-06-21 10:15")), "\(t.rise)")
        #expect(Self.near(t.set, Self.utc("2026-06-22 01:29")), "\(t.set)")
    }

    @Test func chicagoWinterSolstice() throws {
        let t = try #require(SunTimes.riseSet(on: chicago("2026-12-21T12:00:00"), lat: Self.loop.lat, lon: Self.loop.lon))
        #expect(Self.near(t.rise, Self.utc("2026-12-21 13:15")), "\(t.rise)")
        #expect(Self.near(t.set, Self.utc("2026-12-21 22:22")), "\(t.set)")
    }

    /// 8 pm in Chicago is already tomorrow in UTC; it must still get today's sunset, so it reads dusk.
    @Test func chicagoSummerEveningIsStillToday() throws {
        let evening = chicago("2026-06-21T20:00:00")
        let t = try #require(SunTimes.riseSet(on: evening, lat: Self.loop.lat, lon: Self.loop.lon))
        #expect(Self.near(t.set, Self.utc("2026-06-22 01:29")), "\(t.set)")
        #expect(SunTimes.preset(at: evening, rise: t.rise, set: t.set) == .dusk)
    }

    @Test func presetEdges() {
        let rise = Self.utc("2026-06-21 10:15"), set = Self.utc("2026-06-22 01:29")
        func at(_ base: Date, _ minutes: Double) -> LightPreset {
            SunTimes.preset(at: base.addingTimeInterval(minutes * 60), rise: rise, set: set)
        }
        #expect(at(rise, -46) == .night)
        #expect(at(rise, -45) == .dawn)
        #expect(at(rise, 45) == .dawn)
        #expect(at(rise, 46) == .day)
        #expect(at(set, -46) == .day)
        #expect(at(set, -45) == .dusk)
        #expect(at(set, 45) == .dusk)
        #expect(at(set, 46) == .night)
    }

    @Test func polarDayReturnsNil() {
        #expect(SunTimes.riseSet(on: Self.utc("2026-06-21 12:00"), lat: 89.9, lon: 0) == nil)
    }
}
