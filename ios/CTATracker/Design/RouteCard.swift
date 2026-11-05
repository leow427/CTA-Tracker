import CTAKit
import SwiftUI

/// One card, the face of a thick tile filled with its line's color (app spec 4.3, redesign spec 3).
/// `TileStyle` adds the edge, highlight, shadows, and press.
struct RouteCard: View {
    let line: Line
    let kind: Kind
    let stopName: String
    let predictions: [Prediction]   // this card's arrivals, soonest first
    var alert = false
    var freshness: Freshness = .fresh
    var night = false
    /// No data has ever arrived for this stop (CTA unavailable from the start): say nothing rather than "No arrivals".
    var unknown = false
    let now: Date

    @Environment(\.dynamicTypeSize) private var typeSize
    @Environment(\.colorScheme) private var scheme

    // Stale cards dim their fill, not the whole card, so the text keeps 4.5:1 (app spec 8).
    private var rgb: UInt32 {
        let base = LineColors.tile(line.route)
        return freshness == .fresh ? base : LineColors.dimmed(base, dark: scheme == .dark)
    }
    private var next: Prediction? { predictions.first }
    private var later: [Prediction] { Array(predictions.dropFirst().prefix(2)) }

    /// A row that stacks at accessibility text sizes, so text wraps instead of clipping.
    private func row(_ alignment: VerticalAlignment, _ spacing: CGFloat) -> AnyLayout {
        typeSize.isAccessibilitySize
            ? AnyLayout(VStackLayout(alignment: .leading, spacing: Theme.Space.s))
            : AnyLayout(HStackLayout(alignment: alignment, spacing: spacing))
    }

    var body: some View {
        let text = Color(rgb: LineColors.textColor(on: rgb))
        let layout = row(.top, Theme.Space.m)
        VStack(alignment: .leading, spacing: Theme.Space.m) {
            layout {
                VStack(alignment: .leading, spacing: Theme.Space.xs) {
                    // The big route number ("66") or line name ("Blue"), rounded heavy (redesign spec 3).
                    Text(kind == .bus ? line.route : Self.shortName(line))
                        .font(Theme.rounded(.largeTitle, .heavy)).tracking(-0.5)
                        .lineLimit(1).minimumScaleFactor(0.6)
                    Label {
                        Text(next?.dest ?? line.dir)
                    } icon: {
                        Image(systemName: "arrow.right.circle.fill")
                    }
                    .font(Theme.rounded(.title3, .bold))
                    .labelStyle(TightLabel())
                    Text(stopName).font(Theme.rounded(.subheadline, .medium))
                }
                if !typeSize.isAccessibilitySize { Spacer(minLength: 0) }
                ArrivalView(next: next, night: night, unknown: unknown, fresh: freshness == .fresh, now: now).layoutPriority(1)
            }
            footer
        }
        .foregroundStyle(text)
        .padding(Theme.Space.l)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(Color(rgb: rgb), in: .rect(cornerRadius: Theme.cardRadius, style: .continuous))
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(CardSpeech.sentence(line: line, kind: kind, stopName: stopName, next: next, later: later,
                                                alert: alert, freshness: freshness, now: now, night: night))
    }

    @ViewBuilder private var footer: some View {
        let also = ArrivalFormat.also(later, now: now)
        let delayed = next?.delayed == true
        let aged: Int? = if case .aged(let m) = freshness { m } else { nil }
        let layout = row(.center, Theme.Space.s)
        if also != nil || delayed || alert || aged != nil {
            layout {
                if let also { Text(also).font(Theme.rounded(.footnote, .semibold)) }
                if delayed {
                    Text("Delayed").font(Theme.rounded(.caption, .bold))
                        .foregroundStyle(.black)
                        .padding(.horizontal, Theme.Space.s).padding(.vertical, Theme.Space.xs)
                        .background(Theme.amber, in: .capsule)
                        .fixedSize()
                }
                if alert {
                    GlyphIcon(image: Glyphs.alert)
                        .padding(.horizontal, Theme.Space.s).padding(.vertical, Theme.Space.xs)
                        .background(.white.opacity(0.2), in: .capsule)   // a chip on the tile (redesign spec 3)
                }
                if let aged { Text("updated \(aged) min ago").font(Theme.rounded(.footnote, .medium)) }
            }
        }
    }
}

extension RouteCard {
    /// "Blue", "Brown", "Purple": a train line's name without " Line".
    static func shortName(_ line: Line) -> String {
        (LineColors.train(line.route)?.name ?? line.route).replacingOccurrences(of: " Line", with: "")
    }
}

/// Icon and title with the kit's 8 pt gap, on the first line's baseline.
private struct TightLabel: LabelStyle {
    func makeBody(configuration: Configuration) -> some View {
        HStack(alignment: .firstTextBaseline, spacing: Theme.Space.s) { configuration.icon; configuration.title }
    }
}

/// A one-color glyph at footnote size, in the card's text color.
private struct GlyphIcon: View {
    let image: CGImage
    @ScaledMetric(relativeTo: .footnote) private var points: CGFloat = 16
    var body: some View { PixelImage(image: image, points: points, template: true) }
}

/// The big minutes in rounded heavy type (redesign spec 3) with a small "min" under it and the blinking pixel
/// live glyph at its corner. A scheduled arrival shows its clock time with no live glyph.
private struct ArrivalView: View {
    let next: Prediction?
    let night: Bool
    let unknown: Bool
    let fresh: Bool
    let now: Date

    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @ScaledMetric(relativeTo: .largeTitle) private var size: CGFloat = 46

    /// The big part and the small unit under it: ("3", "min"), ("Due", nil), or ("10:05", nil).
    private var parts: (big: String, unit: String?)? {
        guard let p = next else { return nil }
        let m = ArrivalFormat.minutes(until: p.at, now: now)
        return p.live && m >= 1 ? ("\(m)", "min") : (ArrivalFormat.label(p, now: now), nil)
    }

    var body: some View {
        if let parts {
            VStack(alignment: .trailing, spacing: 0) {
                HStack(alignment: .top, spacing: Theme.Space.xs) {
                    Text(parts.big)
                        .font(.system(size: size, weight: .heavy, design: .rounded))
                        .monospacedDigit()
                        .contentTransition(.numericText(countsDown: true))   // minutes roll down as they change
                        .fixedSize()
                    if next?.live == true { LiveGlyph(blinks: fresh).padding(.top, Theme.Space.s) }
                }
                if let unit = parts.unit { Text(unit).font(Theme.rounded(.footnote, .bold)).padding(.top, -Theme.Space.xs) }
            }
            .animation(reduceMotion ? nil : Theme.snappy, value: parts.big)
        } else {
            Text(unknown ? "—" : night ? "No service now" : "No arrivals").font(Theme.rounded(.headline, .bold)).fixedSize()
        }
    }
}

/// Blinks in 2 frames once a second while the data is fresh; stays lit and still on a stale card or under Reduce Motion.
private struct LiveGlyph: View {
    let blinks: Bool
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @ScaledMetric(relativeTo: .title) private var points: CGFloat = 16

    var body: some View {
        TimelineView(.periodic(from: .now, by: 0.5)) { context in
            let lit = !blinks || reduceMotion || Int(context.date.timeIntervalSinceReferenceDate * 2) % 2 == 0
            PixelImage(image: Glyphs.live[lit ? 0 : 1], points: points, template: true)
        }
    }
}

// MARK: - Previews (every card state, light and dark, and the largest text size)

enum PreviewData {
    static let now = Date()
    static func p(_ route: String, _ dest: String, _ min: Double, live: Bool = true, delayed: Bool = false) -> Prediction {
        Prediction(stopID: "", route: route, dir: "", dest: dest, at: now.addingTimeInterval(min * 60 + 10),
                   live: live, delayed: delayed)
    }
    static let redN = Line(route: "Red", dir: "Howard")

    /// A card as the list draws it: the face of a tile.
    @MainActor static func tile(_ card: RouteCard) -> some View {
        Button {} label: { card }.buttonStyle(TileStyle(rgb: LineColors.tile(card.line.route)))
    }

    @MainActor static var cards: some View {
        ScrollView {
            VStack(spacing: Theme.cardGap) {
                tile(RouteCard(line: redN, kind: .station, stopName: "Belmont",
                          predictions: [p("Red", "Howard", 3), p("Red", "Howard", 12), p("Red", "Howard", 20)], now: now))
                tile(RouteCard(line: Line(route: "Brn", dir: "Kimball"), kind: .station, stopName: "Belmont",
                          predictions: [p("Brn", "Kimball", 125, live: false)], now: now))
                tile(RouteCard(line: Line(route: "22", dir: "Northbound"), kind: .bus, stopName: "Clark & Lake",
                          predictions: [p("22", "Howard", 7, delayed: true), p("22", "Howard", 15)], now: now))
                tile(RouteCard(line: Line(route: "Y", dir: "Skokie"), kind: .station, stopName: "Howard",
                          predictions: [p("Y", "Skokie", 4)], alert: true, now: now))
                tile(RouteCard(line: Line(route: "Blue", dir: "O'Hare"), kind: .station, stopName: "Clark/Lake",
                          predictions: [p("Blue", "O'Hare", 0)], now: now))
                tile(RouteCard(line: Line(route: "G", dir: "Harlem/Lake"), kind: .station, stopName: "Clinton",
                          predictions: [], now: now))
                tile(RouteCard(line: Line(route: "Org", dir: "Midway"), kind: .station, stopName: "Halsted",
                          predictions: [p("Org", "Midway", 6)], freshness: .aged(minutes: 7), now: now))
                tile(RouteCard(line: Line(route: "Brn", dir: "Kimball"), kind: .station,
                          stopName: "Harold Washington Library-State/Van Buren",
                          predictions: [p("Brn", "Kimball", 14), p("Brn", "Kimball", 27)], now: now))
                tile(RouteCard(line: Line(route: "X9", dir: "Northbound"), kind: .bus, stopName: "Ashland & Irving Park",
                          predictions: [p("X9", "Ashland Ave & Irving Park", 18)], freshness: .dim(minutes: 3), now: now))
            }
            .padding(.horizontal, Theme.cardMargin)
        }
    }
}

#Preview("Cards, light") { PreviewData.cards }
#Preview("Cards, dark") { PreviewData.cards.preferredColorScheme(.dark) }
#Preview("Largest text, long name") {
    ScrollView {
        VStack(spacing: Theme.cardGap) {
            RouteCard(line: Line(route: "Pink", dir: "54th/Cermak"), kind: .station, stopName: "Washington/Wabash Station",
                      predictions: [PreviewData.p("Pink", "54th/Cermak", 12), PreviewData.p("Pink", "54th/Cermak", 22)],
                      now: PreviewData.now)
            RouteCard(line: Line(route: "22", dir: "Northbound"), kind: .bus, stopName: "Clark & Lake",
                      predictions: [PreviewData.p("22", "Howard", 7, delayed: true), PreviewData.p("22", "Howard", 15)],
                      alert: true, freshness: .aged(minutes: 7), now: PreviewData.now)
        }
        .padding(.horizontal, Theme.cardMargin)
    }
    .dynamicTypeSize(.accessibility5)
}
