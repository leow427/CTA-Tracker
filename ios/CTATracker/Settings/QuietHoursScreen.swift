import SwiftUI

/// Settings → Quiet hours (ecosystem spec 5.2, Settings layout): night for every device. Each device's brightness is
/// on its own page.
struct QuietHoursScreen: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        @Bindable var model = model
        List {
            Section {
                Toggle("Quiet hours", isOn: $model.quietHours.on)
                if model.quietHours.on {
                    DatePicker("Starts", selection: time($model.quietHours.start), displayedComponents: .hourAndMinute)
                    DatePicker("Ends", selection: time($model.quietHours.end), displayedComponents: .hourAndMinute)
                }
            } footer: {
                Text("Trackie sleeps, the dock dims and its lights go dark, and both check the CTA less often.")
                    .foregroundStyle(Theme.secondaryText)
            }
        }
        .animation(Theme.snappy, value: model.quietHours.on)
        .navigationTitle("Quiet hours")
    }

    /// Minutes after midnight as a time today, for the picker, and back.
    private func time(_ minutes: Binding<Int>) -> Binding<Date> {
        Binding {
            // By clock time, not minutes added, so a DST day doesn't shift it an hour.
            let m = minutes.wrappedValue
            return Calendar.current.date(bySettingHour: m / 60, minute: m % 60, second: 0, of: .now) ?? .now
        } set: {
            let c = Calendar.current.dateComponents([.hour, .minute], from: $0)
            minutes.wrappedValue = (c.hour ?? 0) * 60 + (c.minute ?? 0)
        }
    }
}
