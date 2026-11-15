import Foundation
import Testing
@testable import CTAKit

// At most one `nearby` per 10 s, only when the stops changed (ecosystem plan Review Focus 5, spec 5.5).
struct NearbyThrottleTests {
    static let t0 = Date(timeIntervalSince1970: 1_800_000_000)

    static func msg(_ n: Int, lat: Double = 41.9) -> NearbyMessage {
        NearbyMessage(lat: lat, lon: -87.6, trains: ["4\(String(format: "%04d", n))"], buses: [], close: [])
    }

    @Test func panBurstSendsOnce() {
        var t = NearbyThrottle()
        var sent: [NearbyMessage] = []
        for i in 0..<10 {   // 10 offers in 3 s, each with different stops
            if let m = t.offer(Self.msg(i), now: Self.t0.addingTimeInterval(Double(i) * 0.3)) { sent.append(m) }
        }
        #expect(sent.map(\.trains) == [["40000"]])
        #expect(t.flush(now: Self.t0.addingTimeInterval(9.9)) == nil)
        #expect(t.flush(now: Self.t0.addingTimeInterval(10))?.trains == ["40009"])
        #expect(t.flush(now: Self.t0.addingTimeInterval(30)) == nil)   // sent once
    }

    @Test func sameStopsNotResent() {
        var t = NearbyThrottle()
        #expect(t.offer(Self.msg(1), now: Self.t0) != nil)
        // Panned away and back before the next send was due: nothing is left to send.
        #expect(t.offer(Self.msg(2), now: Self.t0.addingTimeInterval(3)) == nil)
        #expect(t.offer(Self.msg(1), now: Self.t0.addingTimeInterval(5)) == nil)
        #expect(t.flush(now: Self.t0.addingTimeInterval(40)) == nil)
        #expect(t.offer(Self.msg(1, lat: 41.95), now: Self.t0.addingTimeInterval(60)) == nil)   // only the center moved
    }

    @Test func closeOnlyChangeIsResent() {
        var t = NearbyThrottle()
        let a = NearbyMessage(lat: 41.9, lon: -87.6, trains: ["41320"], buses: ["1828"], close: ["41320"])
        let b = NearbyMessage(lat: 41.9, lon: -87.6, trains: ["41320"], buses: ["1828"], close: ["1828"])
        #expect(t.offer(a, now: Self.t0) != nil)
        #expect(t.offer(b, now: Self.t0.addingTimeInterval(10))?.close == ["1828"])
    }

    @Test func clockSetBackIsDue() {
        var t = NearbyThrottle()
        #expect(t.offer(Self.msg(1), now: Self.t0) != nil)
        #expect(t.offer(Self.msg(2), now: Self.t0.addingTimeInterval(-3600))?.trains == ["40002"])
        #expect(t.offer(Self.msg(3), now: Self.t0.addingTimeInterval(-3599)) == nil)   // the 10 s restart from there

        var held = NearbyThrottle()
        #expect(held.offer(Self.msg(1), now: Self.t0) != nil)
        #expect(held.offer(Self.msg(2), now: Self.t0.addingTimeInterval(1)) == nil)
        #expect(held.flush(now: Self.t0.addingTimeInterval(-3600))?.trains == ["40002"])
    }

    @Test func dueOfferSendsAtOnce() {
        var t = NearbyThrottle()
        #expect(t.offer(Self.msg(1), now: Self.t0) != nil)
        #expect(t.offer(Self.msg(2), now: Self.t0.addingTimeInterval(10))?.trains == ["40002"])
        #expect(t.flush(now: Self.t0.addingTimeInterval(30)) == nil)
    }
}

extension NearbyThrottleTests {
    @Test func directSendIsRemembered() {
        var t = NearbyThrottle()
        #expect(t.offer(Self.msg(1), now: Self.t0) != nil)
        t.sent(Self.msg(2), now: Self.t0.addingTimeInterval(20))   // recenter
        // Panning back to the first stops resends them: the wearable now shows the second.
        #expect(t.offer(Self.msg(1), now: Self.t0.addingTimeInterval(25)) == nil)   // held: within 10 s
        #expect(t.flush(now: Self.t0.addingTimeInterval(30))?.trains == ["40001"])
        t.sent(Self.msg(3), now: Self.t0.addingTimeInterval(60))
        #expect(t.offer(Self.msg(3), now: Self.t0.addingTimeInterval(80)) == nil)   // same stops
    }

    @Test func directSendDropsHeldOffer() {
        var t = NearbyThrottle()
        #expect(t.offer(Self.msg(1), now: Self.t0) != nil)
        #expect(t.offer(Self.msg(2), now: Self.t0.addingTimeInterval(4)) == nil)   // a pan, held
        t.sent(Self.msg(1), now: Self.t0.addingTimeInterval(6))   // recenter before it was due
        #expect(t.flush(now: Self.t0.addingTimeInterval(20)) == nil)   // the pan doesn't overwrite home
    }
}
