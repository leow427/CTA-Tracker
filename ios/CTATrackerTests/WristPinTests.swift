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
        let r = AppModel.resend(followed: follow, followedAt: due, pinned: belmont, paired: true, docked: false,
                                now: due.addingTimeInterval(Config.followEndedSec - 1))
        guard case .follow(let f) = r else { Issue.record("\(String(describing: r))"); return }
        #expect(f.vehicle == "812" && f.stop == belmont)
    }

    @Test func followPastTheGraceClearsWithItsPin() {
        #expect(AppModel.resend(followed: follow, followedAt: due, pinned: belmont, paired: true, docked: false,
                                now: due.addingTimeInterval(Config.followEndedSec)) == nil)
    }

    @Test func aPinWithNoFollowIsResent() {
        let r = AppModel.resend(followed: nil, followedAt: .distantPast, pinned: belmont, paired: true, docked: false,
                                now: due)
        guard case .pin(let p) = r else { Issue.record("\(String(describing: r))"); return }
        #expect(p == belmont)
    }

    @Test func nothingPinnedSendsNothing() {
        #expect(AppModel.resend(followed: nil, followedAt: .distantPast, pinned: nil, paired: true, docked: false, now: due) == nil)
    }

    @Test func aResetWearableGetsNothing() {
        #expect(AppModel.resend(followed: follow, followedAt: due, pinned: belmont, paired: false, docked: false,
                                now: due) == nil)
    }

    @Test func aDockedWearableGetsNothing() {
        #expect(AppModel.resend(followed: follow, followedAt: due, pinned: belmont, paired: true, docked: true,
                                now: due) == nil)
    }

    @Test func pinOfAnotherStopEndsTheFollow() {
        let howard = ConfigStop(type: .train, route: "Red", dir: "Howard", id: "41320", name: "Belmont")
        #expect(AppModel.follow(after: belmont, followed: follow, followedAt: due, now: due)?.vehicle == "812")
        #expect(AppModel.follow(after: howard, followed: follow, followedAt: due, now: due) == nil)
        #expect(AppModel.follow(after: belmont, followed: nil, followedAt: due, now: due) == nil)
    }

    @Test func pinAfterAPassedFollowOfTheSameStopIsResent() {
        let now = due.addingTimeInterval(Config.followEndedSec)
        let kept = AppModel.follow(after: belmont, followed: follow, followedAt: due, now: now)
        #expect(kept == nil)
        let r = AppModel.resend(followed: kept, followedAt: due, pinned: belmont, paired: true, docked: false, now: now)
        guard case .pin(let p) = r else { Issue.record("\(String(describing: r))"); return }
        #expect(p == belmont)
    }
}
