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
}
