import CTAKit
import SwiftUI

/// What the sheet pushes for a stop: the stop (a station or bus stop ID) and the card that was tapped, if any.
struct DetailRoute: Hashable {
    let stopID: String
    let tapped: Line?
}

/// Stop detail inside the sheet (app spec 5.1): the stop's name, ★, how fresh the data is, its alerts, and
/// every line and direction at the stop with up to 4 arrivals each, the tapped group first.
struct StopDetail: View {
    let route: DetailRoute
    @Environment(AppModel.self) private var model
    @Environment(ArrivalsStore.self) private var arrivals
    @State private var selected: Line?
    @State private var width: CGFloat = 0
    @ScaledMetric(relativeTo: .title2) private var pixelPoints: CGFloat = 24

    var body: some View {
        if let stop = model.index?.stop(id: route.stopID) {
            // The group ★ saves: the tapped card's until another is picked (the order doesn't depend on arrivals).
            let chosen = selected ?? route.tapped ?? DetailGroups.groups(stop: stop, tapped: nil, predictions: [], now: .now).first?.line
            TimelineView(.periodic(from: .now, by: 1)) { context in
                content(stop, chosen: chosen, now: context.date)
            }
            .navigationBarTitleDisplayMode(.inline)   // the header carries the name; the bar only holds back
            .toolbar(.visible, for: .navigationBar)
            .sensoryFeedback(.selection, trigger: chosen)   // set from the start, so only a group tap changes it
        } else {
            EmptyState(title: "No arrivals")
        }
    }

    private func content(_ stop: Stop, chosen: Line?, now: Date) -> some View {
        let groups = DetailGroups.groups(stop: stop, tapped: route.tapped,
                                         predictions: arrivals.predictions[stop.id] ?? [], now: now)
        let failing = stop.kind == .station ? arrivals.trainsFailing : arrivals.busesFailing
        let fit = TileFit.choose(groups: groups, width: width, base: PixelScale.scale(art: PixelFont.glyphHeight, points: pixelPoints), now: now)
        let state = DetailState(loaded: arrivals.fetched[stop.id] != nil, unknown: arrivals.book.unknown(stop.id),
                                stale: arrivals.fetched[stop.id].map { ArrivalFormat.freshness(fetched: $0, now: now) != .fresh } ?? false)
        let routes = Set(groups.map(\.line.route))
        let alerts = Array(Set(routes.flatMap { arrivals.alerts[$0] ?? [] })).sorted { $0.id < $1.id }
        return ScrollView {
            VStack(alignment: .leading, spacing: Theme.Space.m) {
                header(stop, groups: groups, chosen: chosen, failing: failing, now: now)
                if failing { UnavailableBanner() }   // the list's outage banner, here too (app spec 8)
                if !alerts.isEmpty { AlertsRow(alerts: alerts) }
                ForEach(groups) { group in
                    GroupCard(group: group, selected: group.line == chosen, state: state, fit: fit, night: Night.now(now), now: now)
                        .onTapGesture { withAnimation(Theme.snappy) { selected = group.line } }
                }
            }
            .padding(.horizontal, Theme.cardMargin)
            .onGeometryChange(for: CGFloat.self) { [margin = Theme.cardMargin] in $0.size.width - 2 * margin } action: { width = $0 }
            .padding(.bottom, Theme.Space.xxl)
        }
    }

    private func header(_ stop: Stop, groups: [DetailGroup], chosen: Line?, failing: Bool, now: Date) -> some View {
        HStack(alignment: .center, spacing: Theme.Space.m) {
            VStack(alignment: .leading, spacing: Theme.Space.xs) {
                Text(stop.name).font(.title2.weight(.bold))
                FreshnessLine(fetched: arrivals.fetched[stop.id], failing: failing, now: now)
            }
            Spacer(minLength: 0)
            if let group = groups.first(where: { $0.line == chosen }) {
                StarButton(favorite: Favorite(stopID: group.stopID, line: group.line))
            }
        }
        .padding(.top, Theme.Space.s)
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
                if fresh { PixelImage(image: Glyphs.live[0], points: 12, template: true) }
                Text(fresh ? "Live · updated \(Self.ago(age))" : "Updated \(Self.ago(age))")
            } else {
                if !failing { Text("Updating…") }   // when failing, the banner says it once
            }
        }
        .font(.subheadline.weight(.medium))
        .foregroundStyle(.primary)   // on the glass sheet, gray can't keep 4.5:1 over a dark map
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
                .foregroundStyle(.primary)
                .frame(width: Theme.mapButton, height: Theme.mapButton)
                .background(Color(uiColor: .secondarySystemBackground), in: Circle())
        }
        .buttonStyle(PressableCard())
        .sensoryFeedback(.impact(weight: .light), trigger: toggles)   // only a ★ tap, not a change of group
        .accessibilityLabel(on ? "Remove from favorites" : "Add to favorites")
    }
}

/// One line and direction: its name, then up to 4 arrival tiles, the soonest filled with the line color.
/// What detail can honestly say about its stop's data, by the list's rules (checkpoint C).
struct DetailState {
    let loaded: Bool     // a fetch has landed
    let unknown: Bool    // never loaded and the last request failed
    let stale: Bool      // older than 2 min
}

/// One pixel scale and unit choice for every tile on the screen, so all rows match: the largest that fits the
/// widest arrival label four tiles across ("16 min", "12:40"), stepping down a whole pixel scale, then dropping "min".
struct TileFit: Equatable {
    let shrink: Int
    let units: Bool

    static let unitWidth: CGFloat = 22   // "min" in caption semibold

    static func choose(groups: [DetailGroup], width: CGFloat, base: Int, now: Date) -> TileFit {
        let options = [TileFit(shrink: 0, units: true), TileFit(shrink: 1, units: true), TileFit(shrink: 1, units: false)]
        guard width > 0 else { return options[0] }
        let tile = (width - 2 * Theme.Space.l - 3 * Theme.Space.s) / CGFloat(DetailGroups.perGroup) - 2 * Theme.Space.s
        let labels = groups.flatMap(\.arrivals).map { p -> (cols: Int, minutes: Bool) in
            let m = ArrivalFormat.minutes(until: p.at, now: now)
            let big = p.live && m >= 1 ? "\(m)" : ArrivalFormat.label(p, now: now)
            return (PixelFont.grid(big)?.first?.count ?? 0, p.live && m >= 1)
        }
        return options.first { fit in
            labels.allSatisfy { l in
                CGFloat(l.cols * max(1, base - fit.shrink)) + (fit.units && l.minutes ? Theme.Space.xs + unitWidth : 0) <= tile
            }
        } ?? options[2]
    }
}

private struct GroupCard: View {
    let group: DetailGroup
    let selected: Bool
    let state: DetailState
    let fit: TileFit
    let night: Bool
    let now: Date

    var body: some View {
        let rgb = LineColors.rgb(group.line.route)
        VStack(alignment: .leading, spacing: Theme.Space.m) {
            HStack(spacing: Theme.Space.s) {
                Capsule().fill(Color(rgb: rgb)).frame(width: Theme.Space.xs, height: Theme.Space.l + Theme.Space.xs)
                Text(group.kind == .bus ? group.line.route : CardSpeech.lineName(group.line, kind: group.kind))
                    .font(.headline.weight(.bold))
                Label { Text(group.line.dir) } icon: { Image(systemName: "arrow.right.circle.fill") }
                    .font(.headline)
                    .lineLimit(1)
            }
            if group.arrivals.isEmpty {
                // "—" when we can't say (never loaded and failed, or stale with everything gone); a placeholder
                // before the first fetch; only then "No arrivals" / "No service now".
                Text(state.unknown || (state.stale && state.loaded) ? "—" : night ? "No service now" : "No arrivals")
                    .font(.subheadline.weight(.medium))
                    .foregroundStyle(Theme.secondaryText)
                    .redacted(reason: state.loaded || state.unknown ? [] : .placeholder)
            } else {
                tiles(rgb)
            }
        }
        .padding(Theme.Space.l)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(Color(uiColor: .secondarySystemBackground), in: .rect(cornerRadius: Theme.cardRadius, style: .continuous))
        .overlay {
            RoundedRectangle(cornerRadius: Theme.cardRadius, style: .continuous)
                // Neutral, so the selection (what ★ saves) shows on every line color, Yellow and Brown included.
                .strokeBorder(Color.primary, lineWidth: selected ? Theme.pinRim : 0)
        }
        .contentShape(.rect(cornerRadius: Theme.cardRadius))
    }

    private func tiles(_ rgb: UInt32) -> some View {
        HStack(spacing: Theme.Space.s) {
            ForEach(Array(group.arrivals.enumerated()), id: \.offset) { n, p in
                Tile(p: p, rgb: rgb, lead: n == 0, stale: state.stale, shrink: fit.shrink, units: fit.units, now: now)
            }
            ForEach(group.arrivals.count..<DetailGroups.perGroup, id: \.self) { _ in Color.clear.frame(maxWidth: .infinity) }
        }
    }
}

/// One departure: pixel minutes (or a clock time), its destination, and Live or Sched (app spec 5.1).
private struct Tile: View {
    let p: Prediction
    let rgb: UInt32
    let lead: Bool
    let stale: Bool   // dims the lead tile and drops "Live", like a stale card (review focus 4)
    @Environment(\.colorScheme) private var scheme
    let shrink: Int
    let units: Bool
    let now: Date

    var body: some View {
        let m = ArrivalFormat.minutes(until: p.at, now: now)
        let big = p.live && m >= 1 ? "\(m)" : ArrivalFormat.label(p, now: now)
        let fill = stale ? LineColors.dimmed(rgb, dark: scheme == .dark) : rgb
        let text = lead ? Color(rgb: LineColors.textColor(on: fill)) : Color.primary
        VStack(alignment: .leading, spacing: Theme.Space.xs) {
            minutes(big, unit: units && p.live && m >= 1, shrink: shrink)
            Text(p.dest.isEmpty ? " " : p.dest).font(.caption.weight(.medium)).lineLimit(1)
            // One status, on one line, always taking its height so tiles in a row match: Delayed beats Live
            // (it's the news); stale data doesn't claim to be live (the header gives its age).
            ZStack(alignment: .leading) {
                Text("Live").hidden()
                if p.delayed {
                    Text("Delayed").foregroundStyle(.black)
                        .padding(.horizontal, Theme.Space.xs).background(Theme.amber, in: .capsule)
                } else if !(p.live && stale) {
                    Text(p.live ? "Live" : "Sched").textCase(.uppercase)
                }
            }
            .font(.caption2.weight(.bold))
            .lineLimit(1)
            .fixedSize()
        }
        .foregroundStyle(text)
        .padding(.horizontal, Theme.Space.s)   // 8 pt sides: clock times and "10 min" fit four across at full scale
        .padding(.vertical, Theme.Space.m)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(lead ? Color(rgb: fill) : Color(uiColor: .tertiarySystemBackground),
                    in: .rect(cornerRadius: Theme.Space.m, style: .continuous))
    }

    private func minutes(_ big: String, unit: Bool, shrink: Int) -> some View {
        HStack(alignment: .firstTextBaseline, spacing: Theme.Space.xs) {
            PixelText(big, points: 24, relativeTo: .title2, shrink: shrink)
            if unit { Text("min").font(.caption.weight(.semibold)).fixedSize() }
        }
        .fixedSize()
    }
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
        .background(Color(uiColor: .secondarySystemBackground), in: .rect(cornerRadius: Theme.cardRadius, style: .continuous))
    }
}
