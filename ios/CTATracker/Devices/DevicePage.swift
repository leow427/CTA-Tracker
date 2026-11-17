import CTAKit
import MapKit
import SwiftUI

/// One device (app spec 6.3; ecosystem spec 5.2, Settings layout): its state from `info`, its stops (dock), its own
/// brightness, the wearable's Wi-Fi and where it was last seen, and Forget.
struct DevicePage: View {
    let link: DeviceLink
    let device: DeviceLink.Device
    @Environment(\.dismiss) private var dismiss
    @Environment(\.openURL) private var openURL
    @Environment(AppModel.self) private var model
    @State private var confirming = false
    @State private var picking: Int?   // the section whose picker is open
    @State private var addingNetwork: SetupFlow?
    @State private var removing = false
    @State private var removeFailed = false

    private var info: InfoMessage? { link.online[device.id] }
    private var wearable: Bool { device.kind == .handheld }
    /// The wearable advertises the end of its MAC, which is its id (handheld mock README).
    private var bluetoothName: String { "Trackie-\(device.id)" }

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
                    if !wearable { LabeledContent("Wi-Fi", value: wifi(info)) }
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
            brightness
            if wearable { pinSection; wifiSection; lastSeenSection }
            Section {
                Button("Forget \(device.name)", role: .destructive) { confirming = true }
            } footer: {
                Text(forgetNote).foregroundStyle(Theme.secondaryText)
            }
        }
        .navigationTitle(device.name)
        .fullScreenCover(item: $addingNetwork) { SetupScreen(flow: $0) }
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

    private func wifi(_ info: InfoMessage) -> String { info.wifi.connected ? info.wifi.ssid : "Not connected" }

    /// Screen brightness on both; the route LEDs on the dock only. Sent to this device alone.
    @ViewBuilder private var brightness: some View {
        let p = model.prefs(device.id)
        let set = { (p: DevicePrefs) in model.setPrefs(p, for: device.id) }
        Section {
            // 10% at least: 0 would leave the wearable's screen dark with no way to see why.
            slider("Screen", "sun.max.fill", value: p.screen, in: 10...100) { var q = p; q.screen = $0; set(q) }
        } header: {
            Text("Screen brightness")
        } footer: {
            if !wearable {
                Text("The dock dims its picture, never below a readable level.").foregroundStyle(Theme.secondaryText)
            }
        }
        if !wearable {
            Section("Route LEDs") {
                Toggle(isOn: Binding { p.ledOn } set: { var q = p; q.ledOn = $0; set(q) }) {
                    Label("Route LEDs", systemImage: "lightbulb.led.fill")
                }
                if p.ledOn {
                    slider("Brightness", "sun.max", value: p.led, in: 10...100) { var q = p; q.led = $0; set(q) }
                }
            }
        }
    }

    private func slider(_ title: String, _ symbol: String, value: Int, in range: ClosedRange<Double>,
                        set: @escaping (Int) -> Void) -> some View {
        VStack(alignment: .leading, spacing: Theme.Space.s) {
            LabeledContent { Text("\(value)%").monospacedDigit() } label: { Label(title, systemImage: symbol) }
            Slider(value: Binding { Double(value) } set: { set(Int($0.rounded())) }, in: range, step: 5)
                .accessibilityLabel(title)
                .accessibilityValue("\(value) percent")
        }
    }

    /// The stop sent to Trackie (spec 5.6), with Remove: `DELETE /pin`, which ends a follow on it too.
    @ViewBuilder private var pinSection: some View {
        if let pin = model.pinned {
            Section {
                VStack(alignment: .leading, spacing: 2) {
                    Text(pin.name)
                    Text(model.followed == nil ? "\(pin.route) · \(pin.dir)" : "\(pin.route) · \(pin.dir) · Following")
                        .font(.subheadline).foregroundStyle(Theme.secondaryText)
                }
                .accessibilityElement(children: .combine)
                Button("Remove", role: .destructive) {
                    removing = true
                    removeFailed = false
                    Task { removeFailed = !(await model.unpin()); removing = false }
                }
                .disabled(info == nil || removing)
            } header: {
                Text("Pinned stop")
            } footer: {
                if removeFailed { Text("Couldn't reach Trackie.").foregroundStyle(Theme.secondaryText) }
            }
        }
    }

    /// `/wifi` goes over Bluetooth only (it carries a password), so adding a network needs the wearable connected so.
    private var wifiSection: some View {
        let bluetooth = link.transport(of: device.id) == .bluetooth
        return Section {
            LabeledContent("Network", value: info.map(wifi) ?? "Not connected")
            Button("Add a network") { addingNetwork = SetupFlow(addingNetworkTo: device, link: link) }
                .disabled(!bluetooth)
        } header: {
            Text("Wi-Fi")
        } footer: {
            if !bluetooth {
                Text(device.peripheral == nil
                     ? "Trackie was set up with a code, without Bluetooth. To add a network, tap Pair again on "
                        + "Trackie and set it up over Bluetooth."
                     : "Adding a network works while Trackie is connected over Bluetooth, near your iPhone.")
                    .foregroundStyle(Theme.secondaryText)
            }
        }
    }

    /// Find my handheld (spec 5.9): a pin where the phone was when Trackie dropped off both links; a tap opens Maps.
    @ViewBuilder private var lastSeenSection: some View {
        Section("Last seen") {
            switch LastSeen.shown(online: info != nil, model.lastSeen[device.id]) {
            case .connectedNow:
                Text("Connected now")
            case .never:
                Text("Not seen offline yet").foregroundStyle(Theme.secondaryText)
            case .seen(let seen):
                if let c = seen.coordinate {
                    Map(initialPosition: .region(MKCoordinateRegion(center: c, latitudinalMeters: 400,
                                                                    longitudinalMeters: 400)),
                        interactionModes: []) {
                        Marker(device.name, coordinate: c)
                    }
                    .frame(height: 180)
                    .listRowInsets(EdgeInsets())
                    .contentShape(Rectangle())
                    .onTapGesture { if let url = mapsURL(c) { openURL(url) } }
                    .accessibilityElement()
                    .accessibilityLabel("Map of where \(device.name) was last seen")
                    .accessibilityAddTraits(.isButton)
                    .accessibilityHint("Opens Maps")
                }
                Text(seen.label())
            }
        }
    }

    private func mapsURL(_ c: CLLocationCoordinate2D) -> URL? {
        var u = URLComponents(string: "https://maps.apple.com/")
        u?.queryItems = [URLQueryItem(name: "ll", value: "\(c.latitude),\(c.longitude)")]
        return u?.url
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
