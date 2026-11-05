import CTAKit
import SwiftUI

/// What the list pushes for a stop: the stop (a station or bus stop ID), the card that was tapped, if any,
/// and the view it zooms out of (a card's or a search row's id).
struct DetailRoute: Hashable {
    let stopID: String
    let tapped: Line?
    let source: String
}

/// Stop detail over the map (app spec 5.1): the map frames the stop with the line's huge number over it
/// (`DetailHeroView`); the navy page under it has "→ destination", a row of big departure tiles, the chips,
/// the alerts, and the stop's other lines, each as a card in its own color.
struct StopDetail: View {
    let route: DetailRoute
    @Environment(AppModel.self) private var model
    @Environment(ArrivalsStore.self) private var arrivals
    @Environment(\.listMetrics) private var metrics
    @State private var selected: Line?

    var body: some View {
        if let stop = model.index?.stop(id: route.stopID) {
            // The group ★ saves: the tapped card's until another is picked (the order doesn't depend on arrivals).
            let chosen = selected ?? route.tapped ?? DetailGroups.groups(stop: stop, tapped: nil, predictions: [], now: .now).first?.line
            TimelineView(.periodic(from: .now, by: 1)) { context in
                content(stop, chosen: chosen, now: context.date)
            }
            .toolbar(.hidden, for: .navigationBar)   // the red × over the map closes it
            .sensoryFeedback(.selection, trigger: chosen)   // set from the start, so only a group tap changes it
            .onChange(of: chosen, initial: true) { hero(stop, chosen: chosen) }
            .onDisappear { model.detailHero = nil }
        } else {
            EmptyState(title: "No arrivals")
        }
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
                    VStack(alignment: .leading, spacing: Theme.Space.xs) {
                        Label { Text(RouteCard.heading(top.line, kind: top.kind, dest: top.arrivals.first?.dest)) }
                            icon: { Image(systemName: "arrow.right.circle.fill") }
                            .font(Theme.rounded(.title, .heavy))
                            .foregroundStyle(Color.cardText(top.line.route))
                        Text(stop.name).font(Theme.rounded(.headline, .semibold)).foregroundStyle(Theme.mist)
                    }
                    DepartureRow(group: top, state: state, night: Night.now(now), now: now)
                    chips(group: top, stop: stop, failing: failing, now: now)
                }
                if failing { UnavailableBanner() }   // the list's outage banner, here too (app spec 8)
                if !alerts.isEmpty { AlertsRow(alerts: alerts) }
                let others = groups.filter { $0.id != top?.id }
                if !others.isEmpty {
                    Text("At this stop").font(Theme.rounded(.footnote, .heavy)).textCase(.uppercase).tracking(1)
                        .foregroundStyle(Theme.mist)
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
            .padding(.horizontal, Theme.cardMargin)
            .padding(.top, Theme.Space.xl)
            .padding(.bottom, Theme.Space.xxl)
            .frame(maxWidth: .infinity, minHeight: metrics.full - metrics.controlsBottom, alignment: .topLeading)
            .background(Theme.navy, in: PixelShape(block: 6, bottom: false))
            // The page's top edge on screen: touches above it go to the map (PassThroughHost).
            .onGeometryChange(for: CGFloat.self) { $0.frame(in: .global).minY } action: { model.detailTop = $0 }
        }
        .containerBackground(.clear, for: .navigation)
    }

    /// Delayed and how fresh the data is, as chips (★ is over the map).
    private func chips(group: DetailGroup, stop: Stop, failing: Bool, now: Date) -> some View {
        HStack(spacing: Theme.Space.s) {
            if group.arrivals.first?.delayed == true {
                Label { Text("Delayed") } icon: { PixelImage(image: Glyphs.alert, points: 16, template: true) }
                    .font(Theme.rounded(.headline, .bold)).foregroundStyle(.black)
                    .padding(.horizontal, Theme.Space.l).frame(height: Theme.mapButton + Theme.Space.xs)
                    .plate(Theme.amber, edge: 4, shape: .small)
            }
            FreshnessLine(fetched: arrivals.fetched[stop.id], failing: failing, now: now)
                .padding(.horizontal, Theme.Space.l).frame(height: Theme.mapButton + Theme.Space.xs)
                .plate(edge: 4, shape: .small)
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
                .foregroundStyle(Theme.mist)
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
        VStack(spacing: Theme.Space.xs) {
            HStack(alignment: .top, spacing: Theme.Space.xs) {
                // 8× or 6× the 7-pixel digits; clock times step down 3 scales to fit the tile.
                PixelText(minutes ? "\(m)" : ArrivalFormat.label(p, now: now), points: lead ? 56 : 42,
                          shrink: minutes ? 0 : 3)
                if p.live && !stale { PixelImage(image: Glyphs.live[0], points: 16, template: true).padding(.top, Theme.Space.s) }
            }
            if minutes { Text("minutes").font(Theme.rounded(.headline, .bold)) }
            Spacer(minLength: 0)
            if p.delayed {
                Text("DELAYED").foregroundStyle(.black)
                    .padding(.horizontal, Theme.Space.s).padding(.vertical, 3)
                    .background(Theme.amber, in: PixelShape.small)
            } else if !p.live {
                Text("SCHEDULED").foregroundStyle(Color.card(route))
                    .padding(.horizontal, Theme.Space.s).padding(.vertical, 3)
                    .background(text, in: PixelShape.small)
            }
        }
        .font(Theme.rounded(.caption, .heavy))
        .foregroundStyle(text)
        .padding(.vertical, Theme.Space.l)
        .padding(.horizontal, Theme.Space.s)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .plate(.card(route), rim: lead ? text : text.opacity(0.25), rimWidth: lead ? 3 : 2)
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
            VStack(alignment: .leading, spacing: 2) {
                Text(group.kind == .bus ? group.line.route : RouteCard.shortName(group.line))
                    .font(Theme.rounded(.title, .heavy))
                Text(RouteCard.heading(group.line, kind: group.kind, dest: next?.dest))
                    .font(Theme.rounded(.headline, .bold))
                    .lineLimit(1)
            }
            Spacer(minLength: 0)
            if let next {
                let m = ArrivalFormat.minutes(until: next.at, now: now)
                let live = next.live && m >= 1
                VStack(alignment: .trailing, spacing: Theme.Space.xs) {
                    PixelText(live ? "\(m)" : ArrivalFormat.label(next, now: now), points: 28, shrink: live ? 0 : 1)
                    if live { Text("min").font(Theme.rounded(.subheadline, .bold)) }
                }
            } else {
                Text(state.unknown || (state.stale && state.loaded) ? "—" : night ? "No service" : "None")
                    .font(Theme.rounded(.title3, .heavy))
            }
        }
        .foregroundStyle(Color.cardText(group.line.route))
        .padding(Theme.Space.l)
        .plate(.card(group.line.route), rim: Color.cardText(group.line.route).opacity(0.25))
        .contentShape(PixelShape())
    }
}

/// "Live · updated 12 s ago" (app spec 5.1), or the data's age once it's stale.
private struct FreshnessLine: View {
    let fetched: Date?
    let failing: Bool   // this stop's kind is failing; the banner below says so
    let now: Date

    var body: some View {
        HStack(spacing: Theme.Space.xs) {
            if let fetched {
                let age = max(0, Int(now.timeIntervalSince(fetched)))
                let fresh = ArrivalFormat.freshness(fetched: fetched, now: now) == .fresh
                if fresh { PixelImage(image: Glyphs.live[0], points: 16, template: true).foregroundStyle(Theme.signal) }
                Text(fresh ? "Live · updated \(Self.ago(age))" : "Updated \(Self.ago(age))")
            } else {
                if !failing { Text("Updating…") }   // when failing, the banner says it once
            }
        }
        .font(Theme.rounded(.subheadline, .semibold))
        .foregroundStyle(.primary)
    }

    static func ago(_ seconds: Int) -> String {
        seconds < 60 ? "\(seconds) s ago" : "\(seconds / 60) min ago"
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
    @State private var open = false

    var body: some View {
        VStack(alignment: .leading, spacing: Theme.Space.m) {
            Button { withAnimation(Theme.snappy) { open.toggle() } } label: {
                HStack(spacing: Theme.Space.s) {
                    PixelImage(image: Glyphs.alert, points: 16, template: true)
                    Text(alerts.count == 1 ? alerts[0].headline : "\(alerts.count) service alerts")
                        .font(.subheadline.weight(.semibold)).lineLimit(2)
                        .multilineTextAlignment(.leading)
                    Spacer(minLength: 0)
                    Image(systemName: "chevron.down").font(.caption.weight(.bold))
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
                }
            }
        }
        .foregroundStyle(.primary)
        .padding(Theme.Space.l)
        // Neutral: amber is only for the Delayed badge (app spec 4.6); the pixel ⚠ carries the warning.
        .plate(edge: 4, shape: .small)
    }
}
