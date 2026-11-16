import CTAKit
import SwiftUI

/// Settings → Devices (app spec 6.3): each known device, Online or Not connected, and the wearable's battery.
struct DeviceList: View {
    let link: DeviceLink

    var body: some View {
        Section("Devices") {
            if link.devices.isEmpty {
                Text("No devices yet. Turn on a dock or Trackie nearby to set it up.")
                    .foregroundStyle(Theme.secondaryText)
            }
            ForEach(link.devices) { device in
                NavigationLink { DevicePage(link: link, device: device) } label: { row(device) }
            }
        }
    }

    private func row(_ device: DeviceLink.Device) -> some View {
        let info = link.online[device.id]
        return HStack {
            Label {
                VStack(alignment: .leading, spacing: 2) {
                    Text(device.name)
                    Text(Self.status(online: info != nil, transport: link.transport(of: device.id)))
                        .font(.subheadline)
                        .foregroundStyle(info != nil ? Theme.live : Theme.secondaryText)
                    if let info, let detail = Self.detail(info) {
                        detail.font(.subheadline).foregroundStyle(Theme.secondaryText)
                    }
                }
            } icon: {
                Image(systemName: device.kind == .dock ? "tv" : "pawprint.fill")
            }
            Spacer()
            if let info, let battery = Self.battery(info.battery, charging: info.charging) {
                Text(battery).font(.subheadline).foregroundStyle(Theme.secondaryText)
            }
        }
    }

    /// "network · updated 2 minutes ago", either half when only it is known.
    private static func detail(_ info: InfoMessage) -> Text? {
        let ssid = info.wifi.connected ? info.wifi.ssid : nil
        let updated = info.lastUpdate > 0 ? Date(timeIntervalSince1970: TimeInterval(info.lastUpdate)) : nil
        switch (ssid, updated) {
        case let (ssid?, updated?): return Text("\(ssid) · updated \(updated, format: .relative(presentation: .named))")
        case let (ssid?, nil): return Text(ssid)
        case let (nil, updated?): return Text("Updated \(updated, format: .relative(presentation: .named))")
        case (nil, nil): return nil
        }
    }

    // MARK: - Pure parts (tested)

    static func status(online: Bool, transport: DeviceLink.Transport?) -> String {
        guard online || transport != nil else { return "Not connected" }
        return transport.map { "Online \($0.label)" } ?? "Online"
    }

    /// nil for no battery (the dock reports `null`).
    nonisolated static func battery(_ percent: Int?, charging: Bool) -> String? {
        guard let percent else { return nil }
        return charging ? "\(percent)%, charging" : "\(percent)%"
    }
}
