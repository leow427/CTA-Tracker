import CTAKit
import SwiftUI

/// One device (app spec 6.3): its state from `info`, its stops (dock), and Forget.
struct DevicePage: View {
    let link: DeviceLink
    let device: DeviceLink.Device
    @Environment(\.dismiss) private var dismiss
    @Environment(AppModel.self) private var model
    @State private var confirming = false
    @State private var picking: Int?   // the section whose picker is open

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
                    // Read on `.fetchConfig` (AppModel), so a pick made on the dock shows here within a poll.
                    if info != nil, let picks = model.dockPicks[device.id] {
                        ForEach(Array(picks.enumerated()), id: \.offset) { i, pick in
                            Button { picking = i } label: {
                                VStack(alignment: .leading, spacing: 2) {
                                    Text(pick.stop?.name ?? "Empty")
                                        .foregroundStyle(pick.stop == nil ? Theme.secondaryText : .primary)
                                    Text(pick.stop.map { "Section \(i + 1) · \($0.route) · \($0.dir)" } ?? "Section \(i + 1)")
                                        .font(.subheadline).foregroundStyle(Theme.secondaryText)
                                }
                            }
                            .accessibilityHint("Picks this section's stop")
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
        .sheet(isPresented: Binding { picking != nil } set: { if !$0 { picking = nil } }) {
            if let picking { DockStopPicker(dock: device.id, section: picking) }
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

/// The dock's `GET /config` and `POST /config` stops: always 3 sections, `{}` for an empty one (firmware picks.cpp
/// `encodePicks`, `decodePicks`). Trains are a platform ID with its line's direction, as the dock's own picker saves them.
nonisolated struct DockConfig: Codable {
    static let sections = 3   // the dock's kDockStops

    /// One section. `rtnm` (a bus route's name, which the app doesn't know) is kept so a section the app didn't change
    /// goes back exactly as the dock gave it.
    struct Pick: Codable, Hashable {
        var stop: ConfigStop?
        var rtnm = ""
        private enum Key: String, CodingKey { case rtnm }

        init(_ stop: ConfigStop?) { self.stop = stop }
        init(from decoder: Decoder) throws {
            stop = try? ConfigStop(from: decoder)
            rtnm = (try? decoder.container(keyedBy: Key.self).decode(String.self, forKey: .rtnm)) ?? ""
        }
        func encode(to encoder: Encoder) throws {
            guard let stop else { _ = encoder.container(keyedBy: Key.self); return }   // `{}`
            try stop.encode(to: encoder)
            if !rtnm.isEmpty {
                var c = encoder.container(keyedBy: Key.self)
                try c.encode(rtnm, forKey: .rtnm)
            }
        }
    }

    var stops: [Pick]?

    /// Exactly 3 sections, which is all the dock takes; a short list is padded with empty ones.
    static func padded(_ picks: [Pick]) -> [Pick] {
        Array((picks + Array(repeating: Pick(nil), count: sections)).prefix(sections))
    }

    /// All 3 sections with one replaced and the others as they were.
    static func replacing(_ current: [Pick], section: Int, with stop: ConfigStop?) -> [Pick] {
        var picks = padded(current)
        picks[section] = Pick(stop)
        return picks
    }
}

extension ConfigStop {
    /// What a dock section holds for one card: a train's platform (not its station) or a bus stop, with the line.
    init(dock line: Line, kind: Kind, stopID: String, name: String) {
        self.init(type: kind == .station ? .train : .bus, route: line.route, dir: line.dir, id: stopID, name: name)
    }
}
