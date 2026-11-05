import CTAKit
import SwiftUI

/// The always-up sheet: search bar, All · Trains · Buses, ★ favorites, and the nearby cards (app spec 3, 4.2).
struct RouteSheet: View {
    @Binding var detent: PresentationDetent
    @Environment(AppModel.self) private var model
    @Environment(ArrivalsStore.self) private var arrivals
    @Environment(\.scenePhase) private var scenePhase
    @State private var query = ""

    var body: some View {
        @Bindable var model = model
        NavigationStack {
            ScrollView {
                // One clock for every card, outside the lazy stack, so cards are built only as they scroll in.
                TimelineView(.periodic(from: .now, by: 15)) { context in
                    LazyVStack(spacing: Theme.cardGap) {
                        if arrivals.unavailable { UnavailableBanner() }
                        content(now: context.date)
                    }
                    .padding(.horizontal, Theme.cardMargin)
                    .padding(.bottom, Theme.Space.xxl)
                }
            }
            .scrollDismissesKeyboard(.immediately)
            .refreshable { await arrivals.refresh() }
            .safeAreaInset(edge: .top, spacing: 0) {
                VStack(spacing: Theme.Space.m) {
                    SearchBar(query: $query, onFocus: { detent = .large })
                    KindSwitch(filter: $model.filter)
                }
                .padding(.horizontal, Theme.cardMargin)
                .padding(.top, Theme.Space.l)
                .padding(.bottom, Theme.Space.m)
                .background(.bar)
            }
            .toolbar(.hidden, for: .navigationBar)
        }
        .tint(.primary)   // no system blue in the sheet: line colors and amber are the only saturated colors (4.6)
        // Where the sheet's top edge is on screen, so the map frames and follows only what's left visible.
        .onGeometryChange(for: CGFloat.self) { $0.frame(in: .global).minY } action: { model.sheetTop = $0 }
        .sheet(isPresented: $model.showSettings) { SettingsScreen() }
        .onAppear { track() }
        .onChange(of: model.slots) { track() }
        .onChange(of: model.location.denied) { track() }
        // Polls only while the list is on screen: the app is active and nothing covers the sheet (app spec 4.4).
        .onChange(of: scenePhase, initial: true) { updatePolling() }
        .onChange(of: model.showSettings) { updatePolling() }
    }

    @ViewBuilder private func content(now: Date) -> some View {
        let slots = model.slots
        if model.index == nil {
            EmptyState(title: "CTA data unavailable")
        } else {
            ForEach(slots.favorites) { Card(slot: $0, now: now) }   // the ★ section (checkpoint D adds its header)
            if model.location.denied {
                LocationOff()
            } else if slots.farFromChicago {
                EmptyState(title: "No CTA stops near here")
            } else {
                ForEach(CardOrder.sorted(slots.nearby, predictions: allPredictions(slots.nearby), now: now)) {
                    Card(slot: $0, now: now)
                }
            }
        }
    }

    private func allPredictions(_ slots: [Slot]) -> [Prediction] {
        Set(slots.map(\.fetchID)).flatMap { arrivals.predictions[$0] ?? [] }
    }

    private func updatePolling() {
        arrivals.setActive(scenePhase == .active && !model.showSettings)
    }

    /// Only cards on screen are fetched (app spec 4.4): favorites, plus nearby unless location is off.
    private func track() {
        arrivals.track(model.slots.favorites + (model.location.denied ? [] : model.slots.nearby))
    }
}

/// A RouteCard fed from the store, a gray placeholder until its stop's first fetch lands.
struct Card: View {
    let slot: Slot
    let now: Date
    @Environment(AppModel.self) private var model
    @Environment(ArrivalsStore.self) private var arrivals

    var body: some View {
        let loaded = arrivals.fetched[slot.fetchID] != nil
        let predictions = arrivals.cardPredictions(slot, now: now)
        let freshness = arrivals.freshness(slot, now: now)
        // "—" when we can't say: never loaded and the request failed, or stale with every arrival already gone.
        let unknown = arrivals.unknown(slot) || (freshness != .fresh && predictions.isEmpty)
        RouteCard(line: slot.line, kind: slot.kind, stopName: model.stop(for: slot)?.name ?? "",
                  predictions: predictions, alert: arrivals.hasAlert(slot),
                  freshness: freshness, night: Night.now(now), unknown: unknown, now: now)
            .redacted(reason: loaded || unknown ? [] : .placeholder)
    }
}

/// Night hours, in Chicago time (base spec 6.3: "No service now" during them).
enum Night {
    static func now(_ date: Date) -> Bool {
        let c = CTATime.chicago.dateComponents([.hour, .minute], from: date)
        return CTATime.inNightHours(minuteOfDay: (c.hour ?? 0) * 60 + (c.minute ?? 0),
                                    start: Config.nightStartMin, end: Config.nightEndMin)
    }
}

/// The search field at the top of the sheet. Focusing it raises the sheet to full (app spec 3).
struct SearchBar: View {
    @Binding var query: String
    let onFocus: () -> Void
    @FocusState private var focused: Bool

    var body: some View {
        HStack(spacing: Theme.Space.s) {
            Image(systemName: "magnifyingglass").foregroundStyle(Theme.secondaryText)
            TextField("Search", text: $query, prompt: Text("Search stops and routes").foregroundStyle(Theme.secondaryText))
                .focused($focused)
                .submitLabel(.search)
                .autocorrectionDisabled()
            if !query.isEmpty {
                Button { query = "" } label: { Image(systemName: "xmark.circle.fill").foregroundStyle(.tertiary) }
                    .accessibilityLabel("Clear")
            }
        }
        .font(.body)
        .padding(.horizontal, Theme.Space.m)
        .frame(height: Theme.mapButton)
        // Opaque, so the placeholder keeps 4.5:1 even where the translucent sheet shows the map through.
        .background(Color(uiColor: .secondarySystemBackground), in: .rect(cornerRadius: Theme.Space.m, style: .continuous))
        .onChange(of: focused) { if focused { onFocus() } }
    }
}

/// All · Trains · Buses (app spec 4.2), with selection feedback.
struct KindSwitch: View {
    @Binding var filter: KindFilter

    var body: some View {
        Picker("Show", selection: $filter) {
            Text("All").tag(KindFilter.all)
            Text("Trains").tag(KindFilter.trains)
            Text("Buses").tag(KindFilter.buses)
        }
        .pickerStyle(.segmented)
        .sensoryFeedback(.selection, trigger: filter)
    }
}
