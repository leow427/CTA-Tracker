import CTAKit
import Foundation
import Testing
@testable import CTATracker

// What the app gives back to a rebooted wearable, and what a new pin keeps (ecosystem spec 5.6, 5.7).
struct WristPinTests {
    let belmont = ConfigStop(type: .train, route: "Red", dir: "95th/Dan Ryan", id: "41320", name: "Belmont")
    let due = Date(timeIntervalSince1970: 1_762_700_000)
    var follow: FollowMessage { FollowMessage(stop: belmont, vehicle: "812", dest: "95th/Dan Ryan") }

    @Test func followWithinTheGraceIsResent() {
        let r = AppModel.resend(followed: follow, followedAt: due, pinned: belmont,
                                now: due.addingTimeInterval(Config.departedGraceSec - 1))
        guard case .follow(let f) = r else { Issue.record("\(String(describing: r))"); return }
        #expect(f.vehicle == "812" && f.stop == belmont)
    }

    @Test func followPastTheGraceFallsBackToThePin() {
        let r = AppModel.resend(followed: follow, followedAt: due, pinned: belmont,
                                now: due.addingTimeInterval(Config.departedGraceSec))
        guard case .pin(let p) = r else { Issue.record("\(String(describing: r))"); return }
        #expect(p == belmont)
    }

    @Test func nothingPinnedSendsNothing() {
        #expect(AppModel.resend(followed: nil, followedAt: .distantPast, pinned: nil, now: due) == nil)
    }

    @Test func pinOfAnotherStopEndsTheFollow() {
        let howard = ConfigStop(type: .train, route: "Red", dir: "Howard", id: "41320", name: "Belmont")
        #expect(AppModel.follow(after: belmont, followed: follow)?.vehicle == "812")
        #expect(AppModel.follow(after: howard, followed: follow) == nil)
        #expect(AppModel.follow(after: belmont, followed: nil) == nil)
    }
}
