import CTAKit
import SwiftUI

/// What the list pushes for a stop: the stop (a station or bus stop ID), the card that was tapped, if any,
/// and what opened it (a card's or a search row's id).
struct DetailRoute: Hashable {
    let stopID: String
    let tapped: Line?
    let source: String
}

/// Stop detail over the map (app spec 5.1): the map frames the stop (a bus's huge number over it, `DetailHeroView`),
/// washed into the line's dark shade toward the bottom (`MapScreen.wash`). On the wash: "→ destination" and the stop,
/// a row of big departure tiles, the chips, the alerts, and the stop's other lines, each as a card in its color.
struct StopDetail: View {
    let route: DetailRoute
    @Environment(AppModel.self) private var model
    @Environment(ArrivalsStore.self) private var arrivals
    @Environment(\.listMetrics) private var metrics
    @State private var selected: Line?
    @State private var shown = false   // rises in over the map (the list pushes it without a system transition)
    /// A bus stop's twin across the street: the same route the other way (`OppositeStop`), found once per line.
    @State private var opposite: (stop: Stop, line: Line)?

    var body: some View {
        if let stop = model.index?.stop(id: route.stopID) {
            // The group ★ saves: the tapped card's until another is picked (the order doesn't depend on arrivals).
            let chosen = selected ?? route.tapped ?? DetailGroups.groups(stop: stop, tapped: nil, predictions: [], now: .now).first?.line
            // Redrawn only when something on it can change, not every second (the freshness line keeps its own clock).
            TimelineView(schedule(stop)) {
                content(stop, chosen: chosen, now: max($0.date, .now))   // as `Card` does
            }
            .toolbar(.hidden, for: .navigationBar)   // the red × over the map closes it
            .sensoryFeedback(.selection, trigger: chosen)   // set from the start, so only a group tap changes it
            .onChange(of: chosen, initial: true) {
                hero(stop, chosen: chosen)
                opposite = chosen.flatMap { line in model.index.flatMap { OppositeStop.find(for: stop, line: line, in: $0) } }
            }
        } else {
            EmptyState(title: "No arrivals")
        }
    }

    /// Ticks when the page can next read differently (`DetailGroups.nextChange`).
    private func schedule(_ stop: Stop) -> ChangeSchedule {
        let (predictions, fetched) = (arrivals.predictions[stop.id] ?? [], arrivals.fetched[stop.id])
        return ChangeSchedule { DetailGroups.nextChange(after: $0, stop: stop, predictions: predictions, fetched: fetched) }
    }

    /// Tells the map which line to draw big: the chosen group's line and its own stop (what ★ saves).
    private func hero(_ stop: Stop, chosen: Line?) {
        let group = DetailGroups.groups(stop: stop, tapped: route.tapped, predictions: [], now: .now)
            .first { $0.line == chosen }
        model.detailHero = group.map { DetailHero(line: $0.line, kind: $0.kind, stopID: $0.stopID) }
    }

    private func content(_ stop: Stop, chosen: Line?, now: Date) -> some View {
        let groups = DetailGroups.groups(stop: stop, tapped: route.tapped,
                                         predictions: arrivals.predictions[stop.id] ?? [], now: now)
        let failing = stop.kind == .station ? arrivals.trainsFailing : arrivals.busesFailing
        let state = DetailState(loaded: arrivals.fetched[stop.id] != nil, unknown: arrivals.book.unknown(stop.id),
                                stale: arrivals.fetched[stop.id].map { ArrivalFormat.freshness(fetched: $0, now: now) != .fresh } ?? false)
        let routes = Set(groups.map(\.line.route))
        let alerts = Array(Set(routes.flatMap { arrivals.alerts[$0] ?? [] })).sorted { $0.id < $1.id }
        let top = groups.first { $0.line == chosen } ?? groups.first
        return ScrollView {
            // The open map above the page, framing the stop under the big route number.
            Color.clear.frame(height: max(0, (metrics.full * Theme.detailRest).rounded() - metrics.controlsBottom))
            VStack(alignment: .leading, spacing: Theme.Space.l) {
                if let top {
                    HStack(spacing: Theme.Space.s) {
                        VStack(alignment: .leading, spacing: Theme.Space.xs) {
                            Label { Text(RouteCard.heading(top.line, kind: top.kind, dest: top.arrivals.first?.dest)) }
                                icon: { Text(Image(systemName: "arrow.right.circle.fill")) }   // Text, so a bus outlines it
                                .font(Theme.rounded(.title, .heavy))
                            Text(stop.name).font(Theme.rounded(.headline, .semibold))
                        }
                        .foregroundStyle(Color.cardText(top.line.route))
                        .modifier(HeaderBacking(route: top.line.route, bus: top.kind == .bus))
                        .accessibilityElement(children: .combine)
                        // The color names a train's line on screen; VoiceOver needs it said.
                        .accessibilityLabel("\(CardSpeech.lineName(top.line, kind: top.kind)) to \(top.arrivals.first?.dest ?? top.line.dir), \(stop.name)")
                        // A bus stop serves one direction; its twin across the street serves the other.
                        if let opposite, opposite.line.route == top.line.route {
                            DirectionSwitch(route: top.line.route, to: opposite.line.dir) {
                                if opposite.stop.id == stop.id {
                                    // This stop serves both ways (a terminal): pick the other group, as a tap on it would.
                                    withAnimation(Theme.snappy) { selected = opposite.line }
                                } else {
                                    (model.openLine, model.openStop) = (opposite.line, opposite.stop.id)
                                }
                            }
                        }
                    }
                    DepartureRow(group: top, state: state, night: Night.now(now), now: now)
                    chips(group: top, stop: stop, failing: failing, now: now)
                }
                if failing { UnavailableBanner() }   // the list's outage banner, here too (app spec 8)
                if !alerts.isEmpty { AlertsRow(alerts: alerts, route: top?.line.route ?? "") }
                let others = groups.filter { $0.id != top?.id }
                if !others.isEmpty {
                    Text("At this stop").font(Theme.rounded(.footnote, .heavy)).textCase(.uppercase).tracking(1)
                        .foregroundStyle(Color.cardText(top?.line.route ?? ""))
                        .padding(.top, Theme.Space.s)
                    VStack(spacing: Theme.cardGap) {
                        ForEach(others) { group in
                            Button { withAnimation(Theme.snappy) { selected = group.line } } label: {
                                LineCard(group: group, state: state, night: Night.now(now), now: now)
                            }
                            .buttonStyle(PressableCard())
                        }
                    }
                }
            }
            .outlinedText(Color.cardOutline(top?.line.route ?? ""))   // a bus stop's page matches its cards
            .padding(.horizontal, Theme.cardMargin)
            .padding(.top, Theme.Space.xl)
            .padding(.bottom, Theme.Space.xxl)
            .frame(maxWidth: .infinity, minHeight: metrics.full - metrics.controlsBottom, alignment: .topLeading)
            // The page's top edge on screen: touches above it go to the map (PassThroughHost).
            .onGeometryChange(for: CGFloat.self) { $0.frame(in: .global).minY } action: { model.detailTop = $0 }
        }
        .containerBackground(.clear, for: .navigation)
        .opacity(shown ? 1 : 0)
        .offset(y: shown ? 0 : Theme.Space.xxl * 2)
        .onAppear { withAnimation(Theme.smooth) { shown = true } }
    }

    /// Delayed and how fresh the data is, as chips (★ is over the map).
    private func chips(group: DetailGroup, stop: Stop, failing: Bool, now: Date) -> some View {
        HStack(spacing: Theme.Space.s) {
            if group.arrivals.first?.delayed == true {
                Label { Text("Delayed") } icon: { PixelImage(image: Glyphs.alert, points: 16, template: true) }
                    .font(Theme.rounded(.headline, .bold)).foregroundStyle(.black)
                    .plainText()   // amber stays plain on a bus stop's page
                    .padding(.horizontal, Theme.Space.l).frame(height: Theme.mapButton + Theme.Space.xs)
                    .plate(Theme.amber, edge: 4, shape: .small)
            }
            FreshnessLine(fetched: arrivals.fetched[stop.id], failing: failing, halo: Color.glyphHalo(group.line.route))
                .foregroundStyle(Color.cardText(group.line.route))
                .padding(.horizontal, Theme.Space.l).frame(height: Theme.mapButton + Theme.Space.xs)
                .plate(Color.cardGradient(group.line.route), rim: Color.cardOutline(group.line.route) ?? .white.opacity(0.22),
                       edge: 4, shape: .small)
        }
    }
}

/// The chosen line's next departures as big tiles in a row that scrolls sideways, all in the line's card color:
/// the next one biggest, outlined in the light shade.
private struct DepartureRow: View {
    let group: DetailGroup
    let state: DetailState
    let night: Bool
    let now: Date
    @ScaledMetric(relativeTo: .largeTitle) private var lead: CGFloat = 150
    @ScaledMetric(relativeTo: .largeTitle) private var later: CGFloat = 128

    var body: some View {
        if group.arrivals.isEmpty {
            // "—" when we can't say (never loaded and failed, or stale with everything gone); a placeholder
            // before the first fetch; only then "No arrivals" / "No service now".
            Text(state.unknown || (state.stale && state.loaded) ? "—" : night ? "No service now" : "No arrivals")
                .font(Theme.rounded(.title3, .bold))
                .foregroundStyle(Color.cardText(group.line.route))
                .redacted(reason: state.loaded || state.unknown ? [] : .placeholder)
        } else {
            ScrollView(.horizontal) {
                HStack(alignment: .top, spacing: Theme.Space.m) {
                    ForEach(Array(group.arrivals.enumerated()), id: \.offset) { n, p in
                        DepartureTile(p: p, route: group.line.route, lead: n == 0,
                                      stale: state.stale, now: now)
                            .frame(width: n == 0 ? lead : later, height: lead)
                    }
                }
                .padding(.top, Theme.Space.s).padding(.bottom, Theme.Space.m)   // room for the plates' edge
            }
            .scrollIndicators(.hidden)
            .scrollClipDisabled()
        }
    }
}

/// One departure: big pixel minutes (or a clock time or Due) over "minutes", then Live, Scheduled, or Delayed.
private struct DepartureTile: View {
    let p: Prediction
    let route: String
    let lead: Bool
    let stale: Bool   // stale data doesn't claim to be live (the chip gives its age)
    let now: Date
    var body: some View {
        let m = ArrivalFormat.minutes(until: p.at, now: now)
        let minutes = p.live && m >= 1
        let text = Color.cardText(route)
        let outline = Color.cardOutline(route)
        VStack(spacing: Theme.Space.xs) {
            HStack(alignment: .top, spacing: Theme.Space.xs) {
                // 8× or 6× the 7-pixel digits; clock times step down 3 scales to fit the tile.
                PixelText(minutes ? "\(m)" : ArrivalFormat.label(p, now: now), points: lead ? 56 : 42,
                          shrink: minutes ? 0 : 3, outline: outline)
                // Light green when live (still on a tile; none once stale), blue when scheduled; haloed in gray on a
                // white bus tile.
                if !p.live || !stale {
                    PixelImage(image: Glyphs.live[0], points: 16, template: true)
                        .pixelOutline(Color.glyphHalo(route), pixel: 2)
                        .foregroundStyle(p.live ? Theme.live : Theme.scheduled)
                        .padding(.top, Theme.Space.s)
                }
            }
            if minutes { Text(m == 1 ? "minute" : "minutes").font(Theme.rounded(.headline, .bold)) }
            Spacer(minLength: 0)
            // Tags on solid color stay plain.
            if p.delayed {
                Text("DELAYED").foregroundStyle(.black).plainText()
                    .padding(.horizontal, Theme.Space.s).padding(.vertical, 3)
                    .background(Theme.amber, in: PixelShape.small)
            } else if !p.live {
                ScheduledTag(route: route)
            }
        }
        .font(Theme.rounded(.caption, .heavy))
        .foregroundStyle(text)
        .padding(.vertical, Theme.Space.l)
        .padding(.horizontal, Theme.Space.s)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .plate(Color.cardGradient(route), rim: outline ?? (lead ? text : text.opacity(0.25)), rimWidth: lead ? 3 : 2)
        .opacity(stale ? 0.6 : 1)
        .accessibilityElement(children: .combine)
    }
}

/// Another line and direction at the stop, as a card in its color: name, destination, next arrival in pixel digits.
private struct LineCard: View {
    let group: DetailGroup
    let state: DetailState
    let night: Bool
    let now: Date

    var body: some View {
        let next = group.arrivals.first
        HStack(alignment: .center, spacing: Theme.Space.m) {
            // A bus's number over its heading; a train's color names its line, so just "→ destination".
            VStack(alignment: .leading, spacing: 2) {
                if group.kind == .bus { Text(group.line.route).font(Theme.rounded(.title, .heavy)) }
                Text(RouteCard.heading(group.line, kind: group.kind, dest: next?.dest))
                    .font(Theme.rounded(group.kind == .bus ? .headline : .title2, group.kind == .bus ? .bold : .heavy))
                    .lineLimit(1)
            }
            Spacer(minLength: 0)
            if let next {
                let m = ArrivalFormat.minutes(until: next.at, now: now)
                let live = next.live && m >= 1
                VStack(alignment: .trailing, spacing: Theme.Space.xs) {
                    HStack(alignment: .top, spacing: Theme.Space.xs) {
                        PixelText(live ? "\(m)" : ArrivalFormat.label(next, now: now), points: 28, shrink: live ? 0 : 1,
                                  outline: Color.cardOutline(group.line.route))
                        if !next.live { StillGlyph(halo: Color.glyphHalo(group.line.route)) }   // as on the cards
                    }
                    if live { Text("min").font(Theme.rounded(.subheadline, .bold)) }
                    if !next.live { ScheduledTag(route: group.line.route) }
                }
            } else {
                Text(state.unknown || (state.stale && state.loaded) ? "—" : night ? "No service" : "None")
                    .font(Theme.rounded(.title3, .heavy))
            }
        }
        .foregroundStyle(Color.cardText(group.line.route))
        .padding(Theme.Space.l)
        .plate(Color.cardGradient(group.line.route),
               rim: Color.cardOutline(group.line.route) ?? Color.cardText(group.line.route).opacity(0.25))
        .contentShape(PixelShape())
        .accessibilityElement(children: .ignore)
        .accessibilityLabel([CardSpeech.lineName(group.line, kind: group.kind) + " to " + (next?.dest ?? group.line.dir),
                             next.map { ArrivalFormat.label($0, now: now) + ($0.live ? "" : ", scheduled") }
                                 ?? (night ? "No service now" : "No arrivals")]
            .joined(separator: ", "))
    }
}

/// "Live · updated 12 s ago" (app spec 5.1), or the data's age once it's stale. The one part of the page that
/// changes every second, so the only one on a per-second clock: it ticks as each whole second of age passes, so it
/// turns stale in the same millisecond as the tiles (`DetailGroups.nextChange`).
private struct FreshnessLine: View {
    let fetched: Date?
    let failing: Bool   // this stop's kind is failing; the banner below says so
    var halo: Color? = nil   // a bus's gray, so the light-green glyph reads on white

    var body: some View {
        TimelineView(schedule) { line(now: max($0.date, .now)) }   // as `Card` does
    }

    private var schedule: ChangeSchedule {
        guard let fetched else { return .never }
        return ChangeSchedule { t in
            // A hair past each whole second of age, like `nextChange`, since staleness compares strictly; from a minute
            // on the text counts minutes, so a hair past each whole minute (the dim and stale ages are whole minutes).
            let age = t.timeIntervalSince(fetched)
            let step: TimeInterval = age < 59 ? 1 : 60
            return fetched.addingTimeInterval(((age / step).rounded(.down) + 1) * step + 0.001)
        }
    }

    private func line(now: Date) -> some View {
        HStack(spacing: Theme.Space.xs) {
            if let fetched {
                let age = max(0, Int(now.timeIntervalSince(fetched)))
                let fresh = ArrivalFormat.freshness(fetched: fetched, now: now) == .fresh
                if fresh {
                    PixelImage(image: Glyphs.live[0], points: 16, template: true)
                        .pixelOutline(halo, pixel: 2)
                        .foregroundStyle(Theme.live)
                }
                Text(fresh ? "Live · updated \(Self.ago(age))" : "Updated \(Self.ago(age))")
            } else {
                if !failing { Text("Updating…") }   // when failing, the banner says it once
            }
        }
        .font(Theme.rounded(.subheadline, .semibold))
    }

    static func ago(_ seconds: Int) -> String {
        seconds < 60 ? "\(seconds) s ago" : "\(seconds / 60) min ago"
    }
}

/// A bus header sits on a white bus plate, so its gray reads over the map (the wash is still thin up here); a train's
/// light text keeps its soft shadow instead.
private struct HeaderBacking: ViewModifier {
    let route: String
    let bus: Bool

    func body(content: Content) -> some View {
        if bus {
            content
                .padding(Theme.Space.l)
                .frame(maxWidth: .infinity, alignment: .leading)
                .plate(Color.cardGradient(route), rim: Color.cardOutline(route) ?? .clear, rimWidth: 3)
        } else {
            content.shadow(color: .black.opacity(0.6), radius: 6)
        }
    }
}

/// Switches a bus stop page to the same route the other way, at the stop across the street (Leo 2026-11-10): a bus
/// plate button like ★, its arrows along the route's axis.
private struct DirectionSwitch: View {
    let route: String
    let to: String   // "Northbound"
    let action: () -> Void
    @State private var taps = 0

    var body: some View {
        let vertical = to == "Northbound" || to == "Southbound"
        Button { taps += 1; action() } label: {
            Image(systemName: vertical ? "arrow.up.arrow.down" : "arrow.left.arrow.right")
                .font(Theme.mapSymbol)
                .foregroundStyle(Color.cardText(route))
                .frame(width: Theme.mapButton + Theme.Space.xs, height: Theme.mapButton + Theme.Space.xs)
                .plate(Color.cardGradient(route), rim: Color.cardOutline(route) ?? .white.opacity(0.22), edge: 4,
                       shape: .small)
        }
        .buttonStyle(PressableCard())
        .sensoryFeedback(.impact(weight: .light), trigger: taps)   // as ★
        .accessibilityLabel("Switch to \(to)")
    }
}

/// ★ for one line at one stop (plan 4 decision 1), with a light impact (app spec 4.6).
struct StarButton: View {
    let favorite: Favorite
    @Environment(AppModel.self) private var model
    @State private var toggles = 0

    var body: some View {
        let on = model.isFavorite(favorite)
        Button { model.toggleFavorite(favorite); toggles += 1 } label: {
            Image(systemName: on ? "star.fill" : "star")
                .font(Theme.mapSymbol)
                .foregroundStyle(on ? Theme.headlight : .white)
                .frame(width: Theme.mapButton + Theme.Space.xs, height: Theme.mapButton + Theme.Space.xs)
                .plate(edge: 4, shape: .small)
        }
        .buttonStyle(PressableCard())
        .sensoryFeedback(.impact(weight: .light), trigger: toggles)   // only a ★ tap, not a change of group
        .accessibilityLabel(on ? "Remove from favorites" : "Add to favorites")
    }
}

/// What detail can honestly say about its stop's data, by the list's rules (checkpoint C).
struct DetailState {
    let loaded: Bool     // a fetch has landed
    let unknown: Bool    // never loaded and the last request failed
    let stale: Bool      // older than 2 min
}

/// The stop's routes' Customer Alerts, collapsed under a ⚠ row (app spec 5.1).
private struct AlertsRow: View {
    let alerts: [ServiceAlert]
    let route: String   // the plate takes the page's line shade
    @State private var open = false

    var body: some View {
        VStack(alignment: .leading, spacing: Theme.Space.m) {
            Button { withAnimation(Theme.snappy) { open.toggle() } } label: {
                HStack(spacing: Theme.Space.s) {
                    PixelImage(image: Glyphs.alert, points: 16, template: true)
                        .pixelOutline(Color.cardOutline(route), pixel: CGFloat(PixelScale.scale(art: Glyphs.alert.width, points: 16)))
                    Text(alerts.count == 1 ? alerts[0].headline : "\(alerts.count) service alerts")
                        .font(.subheadline.weight(.semibold)).lineLimit(2)
                        .multilineTextAlignment(.leading)
                    Spacer(minLength: 0)
                    Text(Image(systemName: "chevron.down")).font(.caption.weight(.bold))   // Text, so a bus outlines it
                        .rotationEffect(.degrees(open ? 180 : 0))
                }
                .contentShape(.rect)   // the whole row opens it, not just its text
            }
            .buttonStyle(.plain)
            if open {
                ForEach(alerts, id: \.id) { a in
                    VStack(alignment: .leading, spacing: Theme.Space.xs) {
                        Text(a.headline).font(.subheadline.weight(.bold))
                        Text(AlertsWeather.plainText(a.fullText.isEmpty ? a.shortText : a.fullText)).font(.subheadline)
                    }
                    .plainText()   // paragraphs read better without the bus outline
                }
            }
        }
        .foregroundStyle(Color.cardText(route))
        .padding(Theme.Space.l)
        // The line's shade, not amber: amber is only for the Delayed badge (app spec 4.6); the pixel ⚠ carries the warning.
        .plate(Color.cardGradient(route), rim: Color.cardOutline(route) ?? .white.opacity(0.22), edge: 4, shape: .small)
    }
}
