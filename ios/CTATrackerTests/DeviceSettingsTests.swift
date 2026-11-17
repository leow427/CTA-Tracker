import CTAKit
import Testing
@testable import CTATracker

// One device's `settings` (ecosystem spec 5.2, Settings layout): its own brightness with the shared quiet hours.
struct DeviceSettingsTests {
    @Test func buildsOneDevicesSettings() {
        let quiet = QuietHours(start: 22 * 60, end: 6 * 60)
        #expect(AppModel.settings(DevicePrefs(screen: 60, led: 40), quiet: quiet)
                == DeviceSettings(screen: 60, led: 40, nightStart: 1320, nightEnd: 360))
        // LEDs off send 0; quiet hours off send start == end, which both boards read as no night.
        var off = quiet
        off.on = false
        #expect(AppModel.settings(DevicePrefs(screen: 60, led: 40, ledOn: false), quiet: off)
                == DeviceSettings(screen: 60, led: 0, nightStart: 1320, nightEnd: 1320))
        // Task 13's single settings migrate with nothing lost.
        let old = DeviceSettings(screen: 70, led: 0, nightStart: 60, nightEnd: 300)
        #expect(AppModel.settings(DevicePrefs(old), quiet: QuietHours(old)) == old)
    }

    // A change made on the device (spec 5.2, "On the devices too"): the app adopts a brightness it didn't send.
    @Test func adoptsADevicesOwnChange() {
        let sent = DeviceSettings(screen: 80, led: 60, nightStart: 1320, nightEnd: 420)
        let prefs = DevicePrefs(screen: 80, led: 60)
        func report(_ screen: Int, _ led: Int) -> DeviceSettings {
            DeviceSettings(screen: screen, led: led, nightStart: 0, nightEnd: 0)   // quiet hours are never adopted
        }
        // device changed it: adopt
        #expect(AppModel.adopted(report(40, 60), previous: sent, sent: sent, prefs: prefs, pending: false)
                == DevicePrefs(screen: 40, led: 60))
        #expect(AppModel.adopted(report(80, 0), previous: nil, sent: sent, prefs: prefs, pending: false)
                == DevicePrefs(screen: 80, led: 60, ledOn: false))   // LEDs Off keeps the level
        // the app's own change still going out: keep the app's
        #expect(AppModel.adopted(report(40, 60), previous: sent, sent: sent, prefs: prefs, pending: true) == nil)
        // equal: nothing
        #expect(AppModel.adopted(report(80, 60), previous: report(40, 60), sent: sent, prefs: prefs, pending: false) == nil)
        // a report from before the app's write landed (unchanged since the last one): nothing
        #expect(AppModel.adopted(report(40, 60), previous: report(40, 60), sent: sent, prefs: prefs, pending: false) == nil)
    }
}
