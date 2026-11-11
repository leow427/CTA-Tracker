import CTAKit
import SwiftUI

/// The card list over the map (redesign spec 2): search, All · Trains · Buses, ★ favorites, and the nearby cards.
/// It rests partway down the screen and scrolls up over the map; stop detail pushes inside it.
struct CardList: View {
    @Environment(AppModel.self) private var model
    @Environment(ArrivalsStore.self) private var arrivals
    @Environment(\.scenePhase) private var scenePhase
    @State private var query = ""
    @State private var path: [DetailRoute] = []
    @State private var searching = false   // the search field has focus
    /// The last search, computed off the keystroke (debounced) rather than in every body pass, with its query.
    @State private var found: (query: String, results: StopSearch.Results)?
    /// The search hit whose direction buttons are open.
    @State private var expanded: String?

    var body: some View {
        @Bindable var model = model
        GeometryReader { geo in
            let metrics = ListMetrics(geo)
            // The search bar's bottom edge once it's pinned under the controls.
            let pin = metrics.controlsBottom + Theme.searchHeight + Theme.Space.s
            NavigationStack(path: $path) {
                ScrollViewReader { reader in
                ScrollView {
                    // The order's clock, outside the lazy stack, so cards are built only as they scroll in. Each card
                    // keeps its own clock (`Card`), so a minute ticking over redraws that card alone.
                    TimelineView(orderSchedule) { context in
                        LazyVStack(spacing: 0, pinnedViews: [.sectionHeaders]) {
                            // The open map above the resting list. A spacer, not a content margin: pinned
                            // headers stick at the margin's edge, and this one should stick under the controls.
                            Color.clear.frame(height: metrics.restTop - metrics.controlsBottom - 1)
                            // Scrolled to the top, this pins the search bar with the first result right under it.
                            Color.clear.frame(height: 1).id(Self.top)
                            Section {
                                // The cards float over the map; MapScreen fades it to navy toward the bottom.
                                VStack(spacing: 0) {
                                    if arrivals.unavailable { UnavailableBanner().padding(.horizontal, Theme.cardMargin) }
                                    content(now: max(context.date, .now))   // as `Card` does
                                }
                                .animation(Theme.snappy, value: showingSearch)   // list ⇄ results cross-fade, no pop
                                .padding(.bottom, Theme.Space.xl)
                                .frame(maxWidth: .infinity, minHeight: metrics.full, alignment: .top)
                                // Search results sit on solid navy so they read over the map (revision 3).
                                .background { if showingSearch { Theme.navy } }
                                // With no panel behind the pinned search bar, cards end at its bottom edge, so none
                                // peeks through its pixel corners.
                                .modifier(ClipAbove(y: pin))
                            } header: {
                                header(filter: $model.filter)
                                    // The list's top edge on screen: touches above it go to the map (PassThroughHost).
                                    .onGeometryChange(for: CGFloat.self) { $0.frame(in: .global).minY } action: {
                                        model.listTop = $0
                                    }
                            }
                        }
                        // Always tall enough to pin the header, even with a short list or few search results.
                        .frame(minHeight: metrics.full + metrics.restTop, alignment: .top)
                        // Hidden under stop detail, the cards' clocks stop; back on screen they read the time anew.
                        .environment(\.cardClocksPaused, !path.isEmpty)
                    }
                }
                // Cards scrolled past the pinned header disappear under the top controls, not behind them. The
                // safe-area padding below puts this view's top edge where the header pins, so a clip there does what
                // an alpha mask did, without its offscreen pass.
                .clipShape(BelowTop())
                // Detail floats over the map with no page behind it, so the list fades away while it's open
                // (and stays hidden under a back swipe) instead of showing through.
                .opacity(path.isEmpty ? 1 : 0)
                .animation(Theme.snappy, value: path.isEmpty)
                .scrollDismissesKeyboard(.immediately)
                .refreshable { await arrivals.refresh() }
                .safeAreaPadding(.top, metrics.controlsBottom - metrics.safeTop)
                .toolbar(.hidden, for: .navigationBar)
                .containerBackground(.clear, for: .navigation)
                .navigationDestination(for: DetailRoute.self) {
                    StopDetail(route: $0)
                }
                // Keyed on what's shown, not focus: dropping the keyboard to open a hit's directions mustn't re-run the search.
                .task(id: "\(query)|\(showingSearch)|\(model.filter)|\(model.favorites.count)") { await search() }
                .task(id: searching) {   // focusing search scrolls the header up to the top (redesign spec 2)
                    guard searching else { return }
                    withAnimation(Theme.smooth) { reader.scrollTo(Self.top, anchor: .top) }
                }
                // New results start at the top, even if the list under them was scrolled down.
                .onChange(of: found?.query) { if found != nil { reader.scrollTo(Self.top, anchor: .top) } }
                }
            }
            .environment(\.listMetrics, metrics)
        }
        .tint(Theme.headlight)
        .sheet(isPresented: $model.showSettings) { SettingsScreen() }
        .onAppear { track() }
        .onChange(of: model.slots) { track() }
        .onChange(of: model.location.denied) { track() }
        .onChange(of: path) { opened() }
        .onChange(of: model.closeDetail) { if !path.isEmpty { show(Array(path.dropLast())) } }   // the red × over the map
        .onChange(of: model.openStop) { openTapped() }
        // Polls only while the list is on screen: the app is active and nothing covers it (app spec 4.4).
        .onChange(of: scenePhase, initial: true) { updatePolling() }
        .onChange(of: model.showSettings) { updatePolling() }
    }

    private static let top = "listTop"

    /// The headlight-yellow search bar at the top of the cards.
    private func header(filter: Binding<KindFilter>) -> some View {
        SearchBar(query: $query, focused: $searching, filter: filter)
            .padding(.horizontal, Theme.cardMargin)
            .padding(.bottom, Theme.Space.s)
    }

    /// Search shows while typing, or while focused with favorites to list: all of them, past the ★ section's 3
    /// (app spec 2, 3: "favorites first, then matches").
    private var showingSearch: Bool {
        !query.trimmingCharacters(in: .whitespaces).isEmpty || (searching && !model.allFavoriteSlots.isEmpty)
    }

    @ViewBuilder private func content(now: Date) -> some View {
        let slots = model.slots
        if showingSearch {
            if let found { searchResults(found.results, for: found.query) }
        } else if model.index == nil {
            EmptyState(title: "CTA data unavailable")
        } else {
            if !slots.favorites.isEmpty { SectionHeader(title: "Favorites", symbol: "star.fill") }
            ForEach(slots.favorites) { link($0) }
            if model.location.denied {
                LocationOff()
            } else if slots.farFromChicago {
                EmptyState(title: "No CTA stops near here")
            } else {
                if !slots.favorites.isEmpty && !slots.nearby.isEmpty { SectionHeader(title: "Nearby", symbol: "location.fill") }
                ForEach(CardOrder.sorted(slots.nearby, predictions: allPredictions(slots.nearby), now: now)) { link($0) }
            }
        }
    }

    /// A card that opens its stop's detail, pressing down on touch.
    private func link(_ slot: Slot) -> some View {
        Button { show([DetailRoute(stopID: slot.fetchID, tapped: slot.line, source: slot.id)]) } label: {
            Card(slot: slot)
        }
        .buttonStyle(PressableCard())
        .padding(.horizontal, Theme.cardMargin)
        .padding(.vertical, Theme.cardGap / 2)
    }

    /// Runs the search 0.12 s after the last keystroke (a new one cancels it) and off the main thread, so typing and
    /// the keyboard never wait on a scan, and swaps the results in with a fade.
    private func search() async {
        guard showingSearch, let index = await model.search else { found = nil; expanded = nil; return }   // no stale hits next time
        if found != nil { try? await Task.sleep(for: .milliseconds(120)) }
        guard !Task.isCancelled else { return }
        // All favorites match here, not just the ★ section's 3 (app spec 2).
        let (q, center, favorites, kinds) = (query, model.center, model.allFavoriteSlots, model.filter.kinds)
        let r = await Task.detached(priority: .userInitiated) {
            StopSearch.results(q, center: center, favorites: favorites, kinds: kinds, index: index)
        }.value
        guard !Task.isCancelled, showingSearch else { return }
        withAnimation(Theme.snappy) { found = (q, r) }
    }

    /// Search (app spec 3): favorites first, then matching stops, nearest first. Tapping a stop opens big direction
    /// buttons under it ("Toward O'Hare", "22 · Northbound"), each straight into that line and direction (Leo, 2026-11-10).
    @ViewBuilder private func searchResults(_ r: StopSearch.Results, for query: String) -> some View {
        if !r.favorites.isEmpty { SectionHeader(title: "Favorites", symbol: "star.fill") }
        ForEach(r.favorites) { link($0).transition(.opacity) }
        if !r.stops.isEmpty { SectionHeader(title: "Stops", symbol: "mappin") }
        ForEach(r.stops, id: \.stop.id) { hit in
            VStack(spacing: 0) {
                Button {
                    searching = false   // drop the keyboard so the direction buttons show
                    withAnimation(Theme.snappy) { expanded = expanded == hit.stop.id ? nil : hit.stop.id }
                } label: {
                    StopRow(stop: hit.stop, meters: hit.meters, open: expanded == hit.stop.id)
                }
                .buttonStyle(RowStyle())
                .sensoryFeedback(.selection, trigger: expanded == hit.stop.id)
                if expanded == hit.stop.id {
                    // A fade in place: a move slid them in from the list's top, over the rows above.
                    DirectionButtons(stop: hit.stop) { show([$0]) }.transition(.opacity)
                }
            }
            .transition(.opacity)
        }
        if r.favorites.isEmpty && r.stops.isEmpty && !query.trimmingCharacters(in: .whitespaces).isEmpty {
            EmptyState(title: "No stops or routes match “\(query)”")
        }
    }

    /// A stop icon tapped on the map opens its detail, on the listed card's line when it has one (revision 3); a bus
    /// page's direction switch opens the other stop on its own line.
    private func openTapped() {
        guard let id = model.openStop else { return }
        let line = model.openLine
        (model.openStop, model.openLine) = (nil, nil)
        guard path.last?.stopID != id else { return }   // already open
        // Only cards on screen count: with location off, nearby is hidden.
        let shown = model.slots.favorites + (model.location.denied ? [] : model.slots.nearby)
        let listed = shown.first { $0.fetchID == id && (line == nil || $0.line == line) }
        let route = DetailRoute(stopID: id, tapped: line ?? listed?.line, source: listed?.id ?? id)
        if path.last != route { show([route]) }
    }

    /// Pushes or pops detail without the system's slide or zoom (Leo, 2026-11-10: both showed the list through the
    /// clear detail page, then snapped it away): the list fades (above) and detail rises in over the map.
    private func show(_ routes: [DetailRoute]) {
        var t = Transaction()
        t.disablesAnimations = true
        withTransaction(t) { path = routes }
    }

    /// Opening detail: the map centers on the stop, and only that stop is polled.
    private func opened() {
        model.detailStop = path.last.flatMap { model.index?.stop(id: $0.stopID) }
        // Cleared here, not in detail's onDisappear: swapping one stop for another (a map tap) appears the new
        // page before the old one disappears, which would erase the new page's hero.
        if path.isEmpty { model.detailHero = nil }
        track()
    }

    /// The list itself redraws only when the nearby cards' order can change (a soonest arrival dropping off), and not
    /// at all under stop detail.
    private var orderSchedule: ChangeSchedule {
        guard path.isEmpty, !showingSearch, !model.location.denied else { return .never }
        let nearby = model.slots.nearby
        let predictions = allPredictions(nearby)
        return ChangeSchedule { CardOrder.nextChange(after: $0, nearby, predictions: predictions) }
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

/// Hides the part of a view above `y` on screen with a rectangular clip that follows the scroll, rather than an
/// alpha mask (an offscreen pass, rebuilt every scroll frame). It changes only while something is above `y`.
private struct ClipAbove: ViewModifier {
    let y: CGFloat
    @State private var cut: CGFloat = 0

    func body(content: Content) -> some View {
        content
            .clipShape(TopCut(cut: cut))
            .onGeometryChange(for: CGFloat.self) { max(0, y - $0.frame(in: .global).minY) } action: { cut = $0 }
    }
}

/// A view's frame without its top `cut` points.
nonisolated private struct TopCut: Shape {
    let cut: CGFloat

    func path(in rect: CGRect) -> Path {
        Path(CGRect(x: rect.minX, y: rect.minY + cut, width: rect.width, height: max(0, rect.height - cut)))
    }
}

/// Everything below a view's top edge, reaching past its sides and bottom: the cards still run on under the home
/// indicator.
nonisolated private struct BelowTop: Shape {
    func path(in rect: CGRect) -> Path {
        Path(CGRect(x: rect.minX - rect.width, y: rect.minY, width: rect.width * 3, height: rect.height * 2))
    }
}

/// A RouteCard fed from the store, a gray placeholder until its stop's first fetch lands. It keeps its own clock,
/// ticking only when something it shows can change (`ArrivalFormat.nextChange`), so the rest of the list sits still.
struct Card: View {
    let slot: Slot
    @Environment(AppModel.self) private var model
    @Environment(ArrivalsStore.self) private var arrivals
    @Environment(\.cardClocksPaused) private var paused

    var body: some View {
        // The later of the tick and the clock: after new data the timeline can still hold its last tick's date.
        TimelineView(schedule) { card(now: max($0.date, .now)) }
    }

    private var schedule: ChangeSchedule {
        guard !paused else { return .never }
        let mine = [(arrivals.predictions[slot.fetchID] ?? []).filter(slot.matches)]
        let fetched = arrivals.fetched[slot.fetchID].map { [$0] } ?? []
        // A card shows its next arrival and 2 later ones.
        return ChangeSchedule { ArrivalFormat.nextChange(after: $0, cards: mine, shownPerCard: 3, fetched: fetched) }
    }

    @ViewBuilder private func card(now: Date) -> some View {
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

/// The search field on the panel's edge, in headlight yellow, with the All · Trains · Buses menu at its end.
/// Focusing it scrolls the list up (redesign spec 2).
struct SearchBar: View {
    @Binding var query: String
    @Binding var focused: Bool
    @Binding var filter: KindFilter
    @FocusState private var hasFocus: Bool

    var body: some View {
        HStack(spacing: Theme.Space.m) {
            Image(systemName: "magnifyingglass").font(.title3.weight(.bold))
            TextField("Search", text: $query, prompt: Text("Search stops and routes").foregroundStyle(Theme.navy.opacity(0.6)))
                .focused($hasFocus)
                .submitLabel(.search)
                .autocorrectionDisabled()
            if !query.isEmpty {
                Button { query = "" } label: { Image(systemName: "xmark.circle.fill") }
                    .accessibilityLabel("Clear")
            }
            Rectangle().fill(Theme.navy.opacity(0.2)).frame(width: 1, height: 28)
            KindMenu(filter: $filter)
        }
        .font(Theme.rounded(.title3, .semibold))
        .foregroundStyle(Theme.navy)
        .tint(Theme.navy)
        .padding(.horizontal, Theme.Space.l)
        .frame(height: Theme.searchHeight)
        .plate(Theme.headlight, rim: .white.opacity(0.35))
        .onChange(of: hasFocus) { focused = hasFocus }
        .onChange(of: focused) { if !focused { hasFocus = false } }   // the list can drop the keyboard
    }
}

/// All · Trains · Buses (app spec 4.2) as a menu at the end of the search bar, with selection feedback.
struct KindMenu: View {
    @Binding var filter: KindFilter

    var body: some View {
        Menu {
            Picker("Show", selection: $filter) {
                Label("All", systemImage: "square.stack.fill").tag(KindFilter.all)
                Label("Trains", systemImage: "tram.fill").tag(KindFilter.trains)
                Label("Buses", systemImage: "bus.fill").tag(KindFilter.buses)
            }
        } label: {
            Image(systemName: filter == .trains ? "tram.fill" : filter == .buses ? "bus.fill" : "line.3.horizontal.decrease")
                .frame(width: Theme.Space.xxl, height: Theme.Space.xxl)
                .contentShape(.rect)
        }
        .sensoryFeedback(.selection, trigger: filter)
        .accessibilityLabel("Show \(filter == .trains ? "trains" : filter == .buses ? "buses" : "all")")
    }
}

/// A row presses by lighting its background.
struct RowStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .background(configuration.isPressed ? Theme.raised : Color.clear)
            .animation(Theme.snappy, value: configuration.isPressed)
    }
}

/// A small section title in the list ("Favorites", "Nearby", "Stops"), in mist on navy.
struct SectionHeader: View {
    let title: String
    let symbol: String

    var body: some View {
        Label(title, systemImage: symbol)
            .font(Theme.rounded(.footnote, .heavy))
            .textCase(.uppercase)
            .tracking(1)
            .foregroundStyle(Theme.mist)
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(.horizontal, Theme.cardMargin)
            .padding(.top, Theme.Space.l)
            .padding(.bottom, Theme.Space.xs)
            .accessibilityAddTraits(.isHeader)
    }
}

/// A search hit: the stop's name, its direction (bus stops are one direction each), what serves it, and how far.
struct StopRow: View {
    let stop: Stop
    let meters: Double
    var open = false   // its direction buttons are showing

    var body: some View {
        let lines = (stop.lines ?? []) + (stop.platforms ?? []).flatMap(\.lines)
        let routes = Array(Set(lines.map(\.route))).sorted { a, b in
            let (x, y) = (LineColors.trains.firstIndex { $0.route == a } ?? .max, LineColors.trains.firstIndex { $0.route == b } ?? .max)
            return x != y ? x < y : a.localizedStandardCompare(b) == .orderedAscending
        }
        let dirs = Array(Set((stop.lines ?? []).map(\.dir))).sorted()
        HStack(spacing: Theme.Space.m) {
            VStack(alignment: .leading, spacing: Theme.Space.xs) {
                Text(stop.name).font(Theme.rounded(.title3, .bold)).foregroundStyle(.white).multilineTextAlignment(.leading)
                if stop.kind == .bus, !dirs.isEmpty {
                    Text(dirs.joined(separator: " · ")).font(.subheadline.weight(.medium)).foregroundStyle(Theme.secondaryText)
                }
                // Full names if they fit, then short badges, then line-color dots: pills never wrap.
                ViewThatFits(in: .horizontal) {
                    pills(routes, short: false)
                    pills(routes, short: true)
                    HStack(spacing: Theme.Space.xs) {
                        ForEach(routes, id: \.self) { Circle().fill(Color(rgb: LineColors.accent($0))).frame(width: Theme.Space.m, height: Theme.Space.m) }
                    }
                }
            }
            Spacer(minLength: 0)
            Text(Measurement(value: meters, unit: UnitLength.meters).formatted(.measurement(width: .abbreviated, usage: .road)))
                .font(.subheadline.weight(.medium)).foregroundStyle(Theme.secondaryText).fixedSize()
            Image(systemName: "chevron.down").font(.caption.weight(.bold)).foregroundStyle(Theme.secondaryText)
                .rotationEffect(.degrees(open ? 180 : 0))
        }
        .padding(.horizontal, Theme.cardMargin)
        .padding(.vertical, Theme.Space.l)
        .contentShape(.rect)
        .overlay(alignment: .bottom) { Theme.hairline.frame(height: 1).padding(.leading, Theme.cardMargin) }
    }

    private func pills(_ routes: [String], short: Bool) -> some View {
        HStack(spacing: Theme.Space.xs) {
            ForEach(routes, id: \.self) { route in
                let rgb = LineColors.accent(route)
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
    /// The bottom of the top controls (the pet avatar), where the search header pins.
    var controlsBottom: CGFloat = 0

    init() {}

    init(_ geo: GeometryProxy) {
        safeTop = geo.safeAreaInsets.top
        full = geo.size.height + geo.safeAreaInsets.top + geo.safeAreaInsets.bottom
        restTop = (full * Theme.listRest).rounded()
        controlsBottom = safeTop + Theme.Space.xs + Theme.avatar + Theme.Space.s
    }
}

extension EnvironmentValues {
    @Entry var listMetrics = ListMetrics()
    /// The list is hidden under stop detail, so its cards stop their clocks.
    @Entry var cardClocksPaused = false
}

/// A search hit's directions as big buttons in their line's colors: "Toward O'Hare" for each platform of a station,
/// "22 · Northbound" for each route at a bus stop. Each opens stop detail on that line and direction.
struct DirectionButtons: View {
    let stop: Stop
    let open: (DetailRoute) -> Void

    var body: some View {
        // A bus stop's routes in number order (3, 4, 26, 143, J14); a station's platforms as the data lists them.
        let lines = stop.kind == .bus
            ? (stop.lines ?? []).sorted { $0.route.localizedStandardCompare($1.route) == .orderedAscending }
            : (stop.platforms ?? []).flatMap(\.lines)
        VStack(spacing: Theme.Space.s) {
            ForEach(lines, id: \.self) { line in
                Button { open(DetailRoute(stopID: stop.id, tapped: line, source: stop.id)) } label: {
                    HStack(spacing: Theme.Space.m) {
                        Text(Image(systemName: "arrow.right.circle.fill")).font(.title2)   // Text, so a bus outlines it
                        if stop.kind == .bus {
                            Text(line.route).font(Theme.rounded(.title2, .heavy))
                            Text(line.dir).font(Theme.rounded(.title3, .bold))
                        } else {
                            Text("Toward \(line.dir)").font(Theme.rounded(.title3, .heavy))
                        }
                        Spacer(minLength: 0)
                        Text(Image(systemName: "chevron.right")).font(.headline.weight(.bold))
                    }
                    .foregroundStyle(Color.cardText(line.route))
                    .padding(.horizontal, Theme.Space.l)
                    .frame(minHeight: Theme.mapButton + Theme.Space.m)
                    .outlinedText(Color.cardOutline(line.route))
                    .plate(Color.cardGradient(line.route), rim: Color.cardOutline(line.route) ?? Color.cardText(line.route).opacity(0.3),
                           edge: 4, shape: .small)
                    .contentShape(PixelShape.small)
                }
                .buttonStyle(PressableCard())
                .accessibilityLabel("\(CardSpeech.lineName(line, kind: stop.kind)), \(stop.kind == .bus ? line.dir : "toward " + line.dir)")
            }
        }
        .padding(.horizontal, Theme.cardMargin)
        .padding(.bottom, Theme.Space.l)
    }
}

/// Ticks at `next`'s dates: a view redraws only when what it shows can change, instead of on a fixed clock. A nil
/// date ends the ticks until the view's inputs change.
nonisolated struct ChangeSchedule: TimelineSchedule {
    let next: @Sendable (Date) -> Date?

    /// No ticks after the first draw.
    static let never = ChangeSchedule { _ in nil }

    func entries(from start: Date, mode: TimelineScheduleMode) -> some Sequence<Date> {
        sequence(first: start) { [next] in next($0) }
    }
}
