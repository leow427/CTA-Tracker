import SwiftUI

/// Settings (app spec 6.3), presented from inside the sheet. Milestones 3 and 4 fill the rest.
struct SettingsScreen: View {
    let link: DeviceLink
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            List {
                // What every device shares first; each device's own controls are on its page (spec 5.2).
                Section {
                    NavigationLink { PetSettings() } label: { Label("Trackie", systemImage: "train.side.front.car") }
                    NavigationLink { QuietHoursScreen() } label: { Label("Quiet hours", systemImage: "moon.fill") }
                }
                DeviceList(link: link)
                Section {
                    NavigationLink { AboutScreen() } label: { Label("About", systemImage: "info.circle") }
                }
            }
            .navigationTitle("Settings")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) { Button("Done") { dismiss() } }
            }
        }
        .tint(.primary)
    }
}

/// The app version and the CTA's data attribution line (app spec 6.3; CTA developer terms §III.6).
struct AboutScreen: View {
    var body: some View {
        List {
            LabeledContent("Version", value: Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "")
            Section {
                Text("Data provided by Chicago Transit Authority")
            } footer: {
                Text("This app is not made or endorsed by the CTA.").foregroundStyle(Theme.secondaryText)
            }
        }
        .navigationTitle("About")
    }
}
