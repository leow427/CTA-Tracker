import CoreLocation
import CTAKit
import Foundation
import Testing
@testable import CTATracker

// Find my handheld's decisions (ecosystem spec 5.9).
struct LastSeenTests {
    let now = Date(timeIntervalSince1970: 1_763_300_000)
    let fix = CLLocationCoordinate2D(latitude: 41.94, longitude: -87.65)

    @Test func onlyWentOfflineRecords() throws {
        #expect(LastSeen.record(.cameOnline, fix: fix, now: now) == nil)
        #expect(LastSeen.record(.docked, fix: fix, now: now) == nil)
        let seen = try #require(LastSeen.record(.wentOffline, fix: fix, now: now))
        #expect(seen.coordinate?.latitude == 41.94 && seen.date == now)
        // Location off or no fix yet: the time alone.
        #expect(LastSeen.record(.wentOffline, fix: nil, now: now)?.coordinate == nil)
    }

    @Test func pageShowsConnectedThenTheLastSeen() throws {
        let seen = try #require(LastSeen.record(.wentOffline, fix: fix, now: now))
        #expect(LastSeen.shown(online: true, seen) == .connectedNow)
        #expect(LastSeen.shown(online: false, seen) == .seen(seen))
        #expect(LastSeen.shown(online: false, nil) == .never)
        let label = seen.label(now: now)
        #expect(label.hasPrefix("Last seen ") && !label.contains("unavailable"))
        #expect(LastSeen(date: now).label(now: now).hasSuffix(" · location unavailable"))
    }
}
