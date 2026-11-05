import SwiftUI

/// Settings (app spec 6.3), presented from inside the sheet. Milestones 3 and 4 fill the sections.
struct SettingsScreen: View {
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            List {
                Section {
                    row("Devices", "dot.radiowaves.left.and.right")
                    row("Pet", "pawprint.fill")
                    row("Device settings", "slider.horizontal.3")
                    row("Find my handheld", "location.magnifyingglass")
                }
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

    private func row(_ title: String, _ symbol: String) -> some View {
        NavigationLink {
            EmptyState(title: title, message: "Coming in a later build.")
                .navigationTitle(title)
        } label: {
            Label(title, systemImage: symbol)
        }
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
