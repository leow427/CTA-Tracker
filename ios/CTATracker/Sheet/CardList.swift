import CTAKit
import SwiftUI

/// The card list over the map (redesign spec 2): search, All · Trains · Buses, ★ favorites, and the nearby cards.
/// It rests partway down the screen and scrolls up over the map; stop detail pushes inside it.
struct CardList: View {
    @Environment(AppModel.self) private var model
    @Environment(ArrivalsStore.self) private var arrivals
    @Environment(\.scenePhase) private var scenePhase
    @Environment(\.colorScheme) private var scheme
    @State private var query = ""
    @State private var path: [DetailRoute] = []
    @State private var searching = false   // the search field has focus
    @Namespace private var zoom   // a card grows into its stop detail (redesign spec 4)

    var body: some View {
        @Bindable var model = model
        GeometryReader { geo in
            let metrics = ListMetrics(geo)
            NavigationStack(path: $path) {
                ScrollViewReader { reader in
                ScrollView {
                    // One clock for every card, outside the lazy stack, so cards are built only as they scroll in.
                    TimelineView(.periodic(from: .now, by: 15)) { context in
                        LazyVStack(spacing: Theme.cardGap, pinnedViews: [.sectionHeaders]) {
                            // The open map above the resting list. A spacer, not a content margin: pinned
                            // headers stick at the margin's edge, and this one should stick under the controls.
                            Color.clear.frame(height: metrics.restTop - metrics.controlsBottom - Theme.cardGap)
                            Section {
                                if arrivals.unavailable { UnavailableBanner() }
                                content(now: context.date)
                            } header: {
                                header(filter: $model.filter)
                                    .id(Self.top)   // search focus scrolls this to the top
                                    // The list's top edge on screen: touches above it go to the map (PassThroughHost).
                                    .onGeometryChange(for: CGFloat.self) { $0.frame(in: .global).minY } action: {
                                        model.listTop = $0
                                    }
                            }
                        }
                        .padding(.horizontal, Theme.cardMargin)
                        .padding(.bottom, Theme.Space.xxl)
                        // Always tall enough to pin the header, even with a short list or few search results.
                        .frame(minHeight: metrics.full + metrics.restTop, alignment: .top)
                    }
                }
                // Cards scrolled past the pinned header disappear under the top controls, not behind them.
                .mask {
                    VStack(spacing: 0) {
                        Color.clear.frame(height: metrics.controlsBottom)   // where the header pins
                        Color.black
                    }
                    .ignoresSafeArea()
                }
                .scrollDismissesKeyboard(.immediately)
                .refreshable { await arrivals.refresh() }
                .safeAreaPadding(.top, metrics.controlsBottom - metrics.safeTop)
                // Once the header nears the top controls, a solid background fades in behind the cards.
                .background { background(metrics).ignoresSafeArea() }
                .toolbar(.hidden, for: .navigationBar)
                .containerBackground(.clear, for: .navigation)
                .navigationDestination(for: DetailRoute.self) {
                    StopDetail(route: $0).navigationTransition(.zoom(sourceID: $0.source, in: zoom))
                }
                .task(id: searching) {   // focusing search scrolls the header up to the top (redesign spec 2)
                    guard searching else { return }
                    withAnimation(Theme.smooth) { reader.scrollTo(Self.top, anchor: .top) }
                }
                }
            }
            .environment(\.listMetrics, metrics)
        }
        .tint(.primary)   // no system blue: line colors and amber are the saturated colors (redesign spec 5)
        .sheet(isPresented: $model.showSettings) { SettingsScreen() }
        .onAppear { track() }
        .onChange(of: model.slots) { track() }
        .onChange(of: model.location.denied) { track() }
        .onChange(of: path) { opened() }
        // Polls only while the list is on screen: the app is active and nothing covers it (app spec 4.4).
        .onChange(of: scenePhase, initial: true) { updatePolling() }
        .onChange(of: model.showSettings) { updatePolling() }
    }

    private static let top = "listTop"

    private func header(filter: Binding<KindFilter>) -> some View {
        VStack(spacing: Theme.Space.m) {
            SearchBar(query: $query, focused: $searching)
            KindSwitch(filter: filter)
        }
        .padding(Theme.Space.m)
        .glassBackground(.rect(cornerRadius: Theme.cardRadius + Theme.Space.m, style: .continuous))
        .padding(.bottom, Theme.Space.xs)
    }

    private func background(_ metrics: ListMetrics) -> some View {
        let fade = min(1, max(0, (metrics.controlsBottom + Theme.listFade - model.listTop) / Theme.listFade))
        return (scheme == .dark ? Theme.ink : Theme.paper).opacity(fade)
    }

    @ViewBuilder private func content(now: Date) -> some View {
        let slots = model.slots
        let typed = !query.trimmingCharacters(in: .whitespaces).isEmpty
        // Search shows while typing, or while focused with favorites to list: all of them, past the ★ section's 3
        // (app spec 2, 3: "favorites first, then matches").
        if let search = model.search, typed || (searching && !model.allFavoriteSlots.isEmpty) {
            // All favorites match here, not just the ★ section's 3 (app spec 2).
            searchResults(StopSearch.results(query, center: model.center, favorites: model.allFavoriteSlots,
                                             kinds: model.filter.kinds, index: search), now: now)
        } else if model.index == nil {
            EmptyState(title: "CTA data unavailable")
        } else {
            if !slots.favorites.isEmpty { SectionHeader(title: "Favorites", symbol: "star.fill") }
            ForEach(slots.favorites) { link($0, now: now) }
            if model.location.denied {
                LocationOff()
            } else if slots.farFromChicago {
                EmptyState(title: "No CTA stops near here")
            } else {
                if !slots.favorites.isEmpty && !slots.nearby.isEmpty { SectionHeader(title: "Nearby", symbol: "location.fill") }
                ForEach(CardOrder.sorted(slots.nearby, predictions: allPredictions(slots.nearby), now: now)) {
                    link($0, now: now)
                }
            }
        }
    }

    /// A card that opens its stop's detail, pressing down on touch.
    private func link(_ slot: Slot, now: Date) -> some View {
        NavigationLink(value: DetailRoute(stopID: slot.fetchID, tapped: slot.line, source: slot.id)) {
            Card(slot: slot, now: now)
        }
        .buttonStyle(TileStyle(rgb: LineColors.tile(slot.line.route)))
        .matchedTransitionSource(id: slot.id, in: zoom)
    }

    /// Search (app spec 3): favorites first, then matching stops, nearest first.
    @ViewBuilder private func searchResults(_ r: StopSearch.Results, now: Date) -> some View {
        if !r.favorites.isEmpty { SectionHeader(title: "Favorites", symbol: "star.fill") }
        ForEach(r.favorites) { link($0, now: now) }
        if !r.stops.isEmpty { SectionHeader(title: "Stops", symbol: "mappin") }
        ForEach(r.stops, id: \.stop.id) { hit in
            NavigationLink(value: DetailRoute(stopID: hit.stop.id, tapped: nil, source: hit.stop.id)) {
                StopRow(stop: hit.stop, meters: hit.meters)
            }
            .buttonStyle(PressableCard())
            .matchedTransitionSource(id: hit.stop.id, in: zoom)
        }
        if r.favorites.isEmpty && r.stops.isEmpty && !query.trimmingCharacters(in: .whitespaces).isEmpty {
            EmptyState(title: "No stops or routes match “\(query)”")
        }
    }

    /// Opening detail: the map centers on the stop, and only that stop is polled.
    private func opened() {
        model.detailStop = path.last.flatMap { model.index?.stop(id: $0.stopID) }
        track()
    }

    private func allPredictions(_ slots: [Slot]) -> [Prediction] {
        Set(slots.map(\.fetchID)).flatMap { arrivals.predictions[$0] ?? [] }
    }

    private func updatePolling() {
        arrivals.setActive(scenePhase == .active && !model.showSettings)
    }

    /// Only what's on screen is fetched (app spec 4.4): the open stop's detail, or the list's favorites plus
    /// nearby (unless location is off). List polling pauses while detail is open (app spec 5.2's budget).
    private func track() {
        if let stop = model.detailStop {
            arrivals.track(CardBuilder.slots(of: stop, meters: 0))
        } else {
            // Every favorite, not just the ★ section's 3: the rest show live in search (app spec 2).
            arrivals.track(model.allFavoriteSlots + (model.location.denied ? [] : model.slots.nearby))
        }
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

/// The search field at the top of the list. Focusing it scrolls the list up (redesign spec 2).
struct SearchBar: View {
    @Binding var query: String
    @Binding var focused: Bool
    @FocusState private var hasFocus: Bool

    var body: some View {
        HStack(spacing: Theme.Space.s) {
            Image(systemName: "magnifyingglass").foregroundStyle(Theme.secondaryText)
            TextField("Search", text: $query, prompt: Text("Search stops and routes").foregroundStyle(Theme.secondaryText))
                .focused($hasFocus)
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
        .onChange(of: hasFocus) { focused = hasFocus }
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

/// A small section title in the list ("Favorites", "Nearby", "Stops").
struct SectionHeader: View {
    let title: String
    let symbol: String

    var body: some View {
        Label(title, systemImage: symbol)
            .font(.subheadline.weight(.semibold))
            .foregroundStyle(.primary)   // on the glass sheet, gray can't keep 4.5:1 over a dark map
            .padding(.horizontal, Theme.Space.m)
            .padding(.vertical, Theme.Space.s)
            .glassBackground(Capsule())   // readable over the open map
            .fixedSize()
            .frame(maxWidth: .infinity, alignment: .leading)
            .accessibilityAddTraits(.isHeader)
    }
}

/// A search hit: the stop's name, its direction (bus stops are one direction each), what serves it, and how far.
struct StopRow: View {
    let stop: Stop
    let meters: Double

    var body: some View {
        let lines = (stop.lines ?? []) + (stop.platforms ?? []).flatMap(\.lines)
        let routes = Array(Set(lines.map(\.route))).sorted { a, b in
            let (x, y) = (LineColors.trains.firstIndex { $0.route == a } ?? .max, LineColors.trains.firstIndex { $0.route == b } ?? .max)
            return x != y ? x < y : a.localizedStandardCompare(b) == .orderedAscending
        }
        let dirs = Array(Set((stop.lines ?? []).map(\.dir))).sorted()
        HStack(spacing: Theme.Space.m) {
            VStack(alignment: .leading, spacing: Theme.Space.xs) {
                Text(stop.name).font(.body.weight(.semibold)).foregroundStyle(.primary).multilineTextAlignment(.leading)
                if stop.kind == .bus, !dirs.isEmpty {
                    Text(dirs.joined(separator: " · ")).font(.subheadline.weight(.medium)).foregroundStyle(Theme.secondaryText)
                }
                // Full names if they fit, then short badges, then line-color dots: pills never wrap.
                ViewThatFits(in: .horizontal) {
                    pills(routes, short: false)
                    pills(routes, short: true)
                    HStack(spacing: Theme.Space.xs) {
                        ForEach(routes, id: \.self) { Circle().fill(Color(rgb: LineColors.rgb($0))).frame(width: Theme.Space.m, height: Theme.Space.m) }
                    }
                }
            }
            Spacer(minLength: 0)
            Text(Measurement(value: meters, unit: UnitLength.meters).formatted(.measurement(width: .abbreviated, usage: .road)))
                .font(.subheadline.weight(.medium)).foregroundStyle(Theme.secondaryText).fixedSize()
            Image(systemName: "chevron.right").font(.caption.weight(.bold)).foregroundStyle(Theme.secondaryText)
        }
        .padding(Theme.Space.l)
        .background(Color(uiColor: .secondarySystemBackground), in: .rect(cornerRadius: Theme.cardRadius, style: .continuous))
    }

    private func pills(_ routes: [String], short: Bool) -> some View {
        HStack(spacing: Theme.Space.xs) {
            ForEach(routes, id: \.self) { route in
                let rgb = LineColors.rgb(route)
                let full = LineColors.train(route)?.name ?? route   // "Brown Line"; short is "Brown", never "Brn"
                let name = stop.kind == .bus ? route : short ? full.replacingOccurrences(of: " Line", with: "") : full
                Text(name)
                    .font(.caption.weight(.bold))
                    .foregroundStyle(Color(rgb: LineColors.textColor(on: rgb)))
                    .padding(.horizontal, Theme.Space.s).padding(.vertical, Theme.Space.xs)
                    .background(Color(rgb: rgb), in: .capsule)
                    .fixedSize()
            }
        }
    }
}

/// Where the list sits on screen (redesign spec 2), shared with stop detail.
struct ListMetrics: Equatable {
    var safeTop: CGFloat = 0
    var full: CGFloat = 0
    /// The list's resting top edge, about 55% down the screen.
    var restTop: CGFloat = 0
    /// The bottom of the top controls, where the search header pins.
    var controlsBottom: CGFloat = 0

    init() {}

    init(_ geo: GeometryProxy) {
        safeTop = geo.safeAreaInsets.top
        full = geo.size.height + geo.safeAreaInsets.top + geo.safeAreaInsets.bottom
        restTop = (full * Theme.listRest).rounded()
        controlsBottom = safeTop + Theme.Space.xs + Theme.mapButton + Theme.Space.s
    }
}

extension EnvironmentValues {
    @Entry var listMetrics = ListMetrics()
}
