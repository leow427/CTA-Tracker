import CTAKit
import SwiftUI

/// One device (app spec 6.3): its state from `info`, its stops (dock), and Forget.
struct DevicePage: View {
    let link: DeviceLink
    let device: DeviceLink.Device
    @Environment(\.dismiss) private var dismiss
    @State private var stops: [ConfigStop]?
    @State private var confirming = false

    private var info: InfoMessage? { link.online[device.id] }
    private var wearable: Bool { device.kind == .handheld }
    /// The wearable advertises the end of its MAC, which is its id (handheld mock README).
    private var bluetoothName: String { "CTA-Pet-\(device.id)" }

    var body: some View {
        List {
            Section {
                LabeledContent("Status", value: DeviceList.status(online: info != nil, transport: link.transport(of: device.id)))
                if let info {
                    LabeledContent("Last update") {
                        if info.lastUpdate > 0 {
                            Text(Date(timeIntervalSince1970: TimeInterval(info.lastUpdate)), format: .relative(presentation: .named))
                        } else {
                            Text("Not yet")
                        }
                    }
                    LabeledContent("Wi-Fi", value: info.wifi.connected ? info.wifi.ssid : "Not connected")
                    if let battery = DeviceList.battery(info.battery, charging: info.charging) {
                        LabeledContent("Battery", value: battery)
                    }
                }
            }
            if !wearable {
                Section("Stops") {
                    if let stops, stops.isEmpty {
                        Text("No stops picked yet.").foregroundStyle(Theme.secondaryText)
                    } else if let stops {
                        ForEach(stops, id: \.self) { stop in
                            VStack(alignment: .leading, spacing: 2) {
                                Text(stop.name)
                                Text("\(stop.route) · \(stop.dir)").font(.subheadline).foregroundStyle(Theme.secondaryText)
                            }
                        }
                    } else {
                        Text("Shown when the dock is online.").foregroundStyle(Theme.secondaryText)
                    }
                }
            }
            Section {
                Button("Forget \(device.name)", role: .destructive) { confirming = true }
            } footer: {
                Text(forgetNote).foregroundStyle(Theme.secondaryText)
            }
        }
        .navigationTitle(device.name)
        .task(id: info != nil) {
            guard !wearable, info != nil else { return }
            if let config: ConfigMessage = try? await link.get("/config", from: device.id) { stops = config.stops ?? [] }
        }
        .confirmationDialog("Forget \(device.name)?", isPresented: $confirming, titleVisibility: .visible) {
            Button("Forget", role: .destructive) {
                link.forget(device.id)
                dismiss()
            }
        } message: {
            Text(forgetNote)
        }
    }

    /// The board keeps its token until it's reset; iOS keeps the wearable's Bluetooth bond (apps can't remove it).
    private var forgetNote: String {
        wearable
            ? "To set it up again, tap Pair again on the Trackie's settings page. If that doesn't work, also forget "
                + "“\(bluetoothName)” in iOS Settings → Bluetooth."
            : "The dock stays paired until you tap Pair again on it."
    }
}
