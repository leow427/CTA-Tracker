import CTAKit
import SwiftUI

/// Settings → Device settings (ecosystem spec 5.2): brightness and night hours, written to every device with the pet.
struct DeviceSettingsScreen: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        let s = model.deviceSettings
        List {
            Section {
                // 10% at least: 0 would leave the wearable's screen dark with no way to see why.
                slider("Screen", "sun.max.fill", value: s.screen, in: 10...100) {
                    DeviceSettings(screen: $0, led: s.led, nightStart: s.nightStart, nightEnd: s.nightEnd)
                }
                slider("Dock lights", "lightbulb.led.fill", value: s.led, in: 0...100) {
                    DeviceSettings(screen: s.screen, led: $0, nightStart: s.nightStart, nightEnd: s.nightEnd)
                }
            } header: {
                Text("Brightness")
            } footer: {
                Text("The dock's screen is on or off; it dims at night.").foregroundStyle(Theme.secondaryText)
            }
            Section {
                DatePicker("Starts", selection: time(s.nightStart) {
                    DeviceSettings(screen: s.screen, led: s.led, nightStart: $0, nightEnd: s.nightEnd)
                }, displayedComponents: .hourAndMinute)
                DatePicker("Ends", selection: time(s.nightEnd) {
                    DeviceSettings(screen: s.screen, led: s.led, nightStart: s.nightStart, nightEnd: $0)
                }, displayedComponents: .hourAndMinute)
            } header: {
                Text("Night")
            } footer: {
                Text("Trackie sleeps, the dock dims and its lights go dark, and both check the CTA less often.")
                    .foregroundStyle(Theme.secondaryText)
            }
        }
        .navigationTitle("Device settings")
    }

    private func slider(_ title: String, _ symbol: String, value: Int, in range: ClosedRange<Double>,
                        set: @escaping (Int) -> DeviceSettings) -> some View {
        VStack(alignment: .leading, spacing: Theme.Space.s) {
            LabeledContent { Text("\(value)%").monospacedDigit() } label: { Label(title, systemImage: symbol) }
            Slider(value: Binding { Double(value) } set: { model.deviceSettings = set(Int($0.rounded())) },
                   in: range, step: 5)
                .accessibilityLabel(title)
                .accessibilityValue("\(value) percent")
        }
    }

    /// Minutes after midnight as a time today, for the picker, and back.
    private func time(_ minutes: Int, set: @escaping (Int) -> DeviceSettings) -> Binding<Date> {
        Binding {
            // By clock time, not minutes added, so a DST day doesn't shift it an hour.
            Calendar.current.date(bySettingHour: minutes / 60, minute: minutes % 60, second: 0, of: .now) ?? .now
        } set: {
            let c = Calendar.current.dateComponents([.hour, .minute], from: $0)
            model.deviceSettings = set((c.hour ?? 0) * 60 + (c.minute ?? 0))
        }
    }
}
