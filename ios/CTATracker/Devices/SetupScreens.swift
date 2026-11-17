import CTAKit
import SwiftUI

/// The found card over the list, the app's Bluetooth line before iOS asks (app spec 3), and the setup flow over
/// everything (app spec 6.1–6.2). Attached once, inside the sheet's content.
struct SetupHost: ViewModifier {
    let link: DeviceLink
    @Environment(AppModel.self) private var model
    @State private var dismissed: Set<String> = []   // "Not now": until the next launch
    @State private var flow: SetupFlow?
    @State private var asked = false

    func body(content: Content) -> some View {
        content
            .overlay(alignment: .bottom) {
                Group {
                    if let c = candidates.first {
                        SetupCard(title: c.title, action: c.action, symbol: c.kind == .dock ? "tv" : "applewatch") {
                            let f = SetupFlow(c, link: link)
                            f.petName = model.pet.name
                            f.onPetName = { [model] name in model.pet = PetConfig(name: name, colors: model.pet.colors) }
                            flow = f
                        } notNow: {
                            dismissed.insert(c.id)
                        }
                    } else if BLELink.enabled && needsAsking && !asked {
                        SetupCard(title: "Bluetooth finds your dock and handheld.", action: "Continue",
                                  symbol: "dot.radiowaves.left.and.right") {
                            asked = true
                            link.ble.start()
                        } notNow: { asked = true }
                    }
                }
                .padding(.horizontal, Theme.cardMargin)
                .padding(.bottom, Theme.Space.l)
                .transition(.move(edge: .bottom).combined(with: .opacity))
                .animation(Theme.smooth, value: candidates.first?.id)
            }
            .fullScreenCover(item: $flow) { SetupScreen(flow: $0) }
            #if DEBUG
            .onAppear {
                let args = UserDefaults.standard
                if flow == nil, let demo = args.string(forKey: "setupDemo"),
                   ["pair", "asking", "failed", "code", "wifi", "pet", "ready"].contains(demo) { flow = SetupFlow(demo: demo, link: link) }
            }
            #endif
    }

    private var needsAsking: Bool {
        #if DEBUG
        if UserDefaults.standard.string(forKey: "setupDemo") == "ask" { return true }
        #endif
        return link.ble.needsAsking
    }

    private var candidates: [SetupCandidate] {
        #if DEBUG
        if UserDefaults.standard.string(forKey: "setupDemo") == "card", !dismissed.contains("c3d4") {
            return [SetupCandidate(kind: .handheld, deviceID: "c3d4")]
        }
        #endif
        return SetupFlow.candidates(bleFound: BLELink.enabled ? link.ble.found : [:], unpaired: link.unpaired, known: link.devices, dismissed: dismissed)
    }
}

extension SetupFlow: Identifiable {}

/// "Trackie found · Set up", on a raised pixel plate.
private struct SetupCard: View {
    let title: String
    let action: String
    let symbol: String
    let go: () -> Void
    let notNow: () -> Void

    var body: some View {
        HStack(spacing: Theme.Space.m) {
            Image(systemName: symbol)
                .font(.title3.weight(.semibold))
                .foregroundStyle(Theme.headlight)
                .frame(width: 32)
                .accessibilityHidden(true)
            Text(title)
                .font(Theme.rounded(.subheadline, .bold))
                .foregroundStyle(.white)
                .frame(maxWidth: .infinity, alignment: .leading)
            Button("Not now", action: notNow)
                .font(.subheadline)
                .foregroundStyle(Theme.mist)
            Button(action, action: go)
                .buttonStyle(PrimaryButton(compact: true))
        }
        .padding(Theme.Space.m)
        .plainText()
        .plate(edge: 4, shape: .small)
    }
}

/// One step per screen on night navy: the pet, the step's line, its controls, and the button at the bottom.
struct SetupScreen: View {
    @Bindable var flow: SetupFlow
    @Environment(\.dismiss) private var dismiss
    @FocusState private var focused: Bool

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: Theme.Space.l) {
                    PixelImage(image: Glyphs.pet(mood), points: 128)
                        .padding(.top, Theme.Space.xl)
                        .accessibilityHidden(true)
                    Text(title)
                        .font(Theme.rounded(.title2, .bold))
                        .foregroundStyle(.white)
                        .multilineTextAlignment(.center)
                    if let line {
                        Text(line)
                            .font(.body)
                            .foregroundStyle(Theme.mist)
                            .multilineTextAlignment(.center)
                    }
                    controls
                    if let message = flow.message {
                        Label(message, systemImage: "exclamationmark.triangle.fill")
                            .font(.subheadline.weight(.semibold))
                            .foregroundStyle(Theme.amber)
                            .multilineTextAlignment(.center)
                            .transition(.opacity)
                    }
                }
                .padding(.horizontal, Theme.Space.xl)
                .frame(maxWidth: .infinity)
                .animation(Theme.snappy, value: flow.message)
                .animation(Theme.snappy, value: flow.networks)
            }
            .scrollDismissesKeyboard(.interactively)
            .safeAreaInset(edge: .bottom) { bottom.padding(Theme.Space.l) }
            .background(Theme.navy.ignoresSafeArea())
            .toolbar {
                if flow.step != .ready {
                    ToolbarItem(placement: .cancellationAction) {
                        Button("Cancel") { dismiss() }.foregroundStyle(Theme.mist)
                    }
                }
            }
        }
        .tint(Theme.headlight)
        .task { await flow.start() }
        .onDisappear { flow.close() }
        .interactiveDismissDisabled()
    }

    private var mood: Mood {
        switch flow.step {
        case .ready: .hyped
        case .pet: .happy
        default: flow.message == nil ? .happy : .worried
        }
    }

    private var title: String {
        switch flow.step {
        case .pair: "Pair " + (flow.candidate.kind == .dock ? "your dock" : "Trackie")
        case .wifi: flow.networks.isEmpty ? "Wi-Fi" : "Add another"
        case .pet: "Name your Trackie"
        case .ready: flow.candidate.kind == .dock ? "Your dock is showing live arrivals." : "Trackie is ready."
        }
    }

    private var line: String? {
        switch (flow.step, flow.method) {
        case (.pair, .bluetooth): flow.bluetoothFailed ? hint : "Type the code shown on Trackie when your iPhone asks."
        case (.pair, .code): "Type the code shown on \(flow.deviceName)."
        case (.pair, .waiting): flow.asking ? "Bluetooth finds your dock and handheld." : "Looking for Trackie…"
        case (.pair, .unavailable): "Turn on Bluetooth to set up Trackie."
        case (.wifi, _) where flow.addingNetwork && flow.firstSlot == 0 && flow.networks.isEmpty:
            "This becomes Trackie's home network and replaces the one saved before."
        case (.wifi, _) where flow.addingNetwork: "Trackie uses Wi-Fi when your iPhone isn't near."
        case (.wifi, _): "Optional. Trackie uses Wi-Fi when your iPhone isn't near. Home first."
        case (.pet, _): "Your dock and Trackie share one pet."
        default: nil
        }
    }

    /// The bond on the iPhone may be stale after a reset: only a failed fresh pairing says so (Leo 2026-11-14).
    private var hint: String {
        "If Trackie was reset, forget \(flow.candidate.name ?? "CTA-Pet") in iOS Settings → Bluetooth, then try again."
    }

    @ViewBuilder private var controls: some View {
        switch flow.step {
        case .pair:
            if case .code = flow.method {
                field("000000", text: $flow.code)
                    .keyboardType(.numberPad)
                    .textContentType(.oneTimeCode)
                    .font(.system(.title, design: .monospaced, weight: .bold))
                    .multilineTextAlignment(.center)
                    .onChange(of: flow.code) {
                        flow.code = String(flow.code.filter(\.isNumber).prefix(6))
                        if flow.code.count == 6 { Task { await flow.submitCode() } }
                    }
                    .onAppear { focused = true }
            } else if flow.busy || (flow.method == .waiting && !flow.asking) {
                ProgressView().tint(Theme.headlight).padding(Theme.Space.l)
            }
        case .wifi:
            ForEach(flow.networks, id: \.self) { name in
                Label(name, systemImage: "checkmark.circle.fill")
                    .font(Theme.rounded(.body, .bold))
                    .foregroundStyle(Theme.live)
                    .frame(maxWidth: .infinity, alignment: .leading)
            }
            if flow.canAddNetwork {
                field("Network name", text: $flow.ssid)
                    .textContentType(.none)
                    .autocorrectionDisabled()
                    .textInputAutocapitalization(.never)
                SecureField("Password", text: $flow.password)
                    .modifier(FieldStyle())
                    .focused($focused)
            }
        case .pet:
            field("Name", text: $flow.petName)
                .multilineTextAlignment(.center)
                .font(Theme.rounded(.title3, .bold))
        case .ready:
            EmptyView()
        }
    }

    @ViewBuilder private var bottom: some View {
        VStack(spacing: Theme.Space.m) {
            switch flow.step {
            case .pair:
                if flow.asking {
                    Button("Continue") { Task { await flow.allowBluetooth() } }.buttonStyle(PrimaryButton())
                } else if case let .bluetooth(p) = flow.method, flow.bluetoothFailed {
                    Button("Try again") { Task { await flow.pairOverBluetooth(p) } }.buttonStyle(PrimaryButton())
                    if flow.codeFallback != nil {
                        Button("Use the code on Trackie instead") { flow.useCode() }.buttonStyle(SecondaryButton())
                    }
                } else if case .code = flow.method {
                    Button("Pair") { Task { await flow.submitCode() } }
                        .buttonStyle(PrimaryButton())
                        .disabled(flow.code.count != 6 || flow.busy)
                }
            case .wifi:
                if flow.canAddNetwork {
                    Button(flow.busy ? "Joining…" : "Join") { Task { await flow.joinWifi() } }
                        .buttonStyle(PrimaryButton())
                        .disabled(flow.ssid.isEmpty || flow.busy)
                }
                if flow.addingNetwork {
                    if !flow.networks.isEmpty { Button("Done") { dismiss() }.buttonStyle(SecondaryButton()) }
                } else {
                    Button(flow.networks.isEmpty ? "Skip" : "Continue") { flow.finishWifi() }
                        .buttonStyle(SecondaryButton())
                        .disabled(flow.busy)
                }
            case .pet:
                Button("Continue") { flow.savePet() }
                    .buttonStyle(PrimaryButton())
                    .disabled(flow.petName.trimmingCharacters(in: .whitespaces).isEmpty || flow.busy)
            case .ready:
                Button("Done") { dismiss() }.buttonStyle(PrimaryButton())
            }
        }
    }

    private func field(_ prompt: String, text: Binding<String>) -> some View {
        TextField(prompt, text: text, prompt: Text(prompt).foregroundStyle(Theme.mist.opacity(0.6)))
            .modifier(FieldStyle())
            .focused($focused)
    }
}

/// A text field on a raised pixel plate.
private struct FieldStyle: ViewModifier {
    func body(content: Content) -> some View {
        content
            .foregroundStyle(.white)
            .padding(.horizontal, Theme.Space.l)
            .padding(.vertical, Theme.Space.m)
            .frame(minHeight: 52)
            .plate(edge: 3, shape: .small)
    }
}

/// Headlight yellow on a pixel plate, pressing down like the cards.
private struct PrimaryButton: ButtonStyle {
    var compact = false
    @Environment(\.isEnabled) private var enabled

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(Theme.rounded(compact ? .subheadline : .headline, .bold))
            .foregroundStyle(Theme.navy)
            .padding(.horizontal, compact ? Theme.Space.m : Theme.Space.l)
            .padding(.vertical, compact ? Theme.Space.s : Theme.Space.m)
            .frame(maxWidth: compact ? nil : .infinity, minHeight: compact ? 32 : 50)
            .plate(Theme.headlight, rim: .white.opacity(0.35), edge: compact ? 3 : 4, shape: .small)
            .opacity(enabled ? 1 : 0.45)
            .scaleEffect(configuration.isPressed ? 0.97 : 1)
            .animation(Theme.snappy, value: configuration.isPressed)
    }
}

private struct SecondaryButton: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(Theme.rounded(.headline, .bold))
            .foregroundStyle(.white.opacity(configuration.isPressed ? 0.6 : 1))
            .frame(maxWidth: .infinity, minHeight: 44)
            .contentShape(Rectangle())
    }
}
