import CTAKit
import SwiftUI

/// One route and direction as a card in its line's color (redesign revision 2): a darker shade of the line as the
/// raised pixel plate, a lighter shade for all its text, and big pixel minutes on the right. A bus leads with its
/// huge route number, then "→ destination" and the stop; a train leads with its stop name, then "→ destination",
/// since its color names the line (revision 3).
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
    @ScaledMetric(relativeTo: .largeTitle) private var routeSize: CGFloat = 60
    @ScaledMetric(relativeTo: .largeTitle) private var stationSize: CGFloat = 34

    private var next: Prediction? { predictions.first }
    private var later: [Prediction] { Array(predictions.dropFirst().prefix(2)) }

    /// A row that stacks at accessibility text sizes, so text wraps instead of clipping.
    private func row(_ alignment: VerticalAlignment, _ spacing: CGFloat) -> AnyLayout {
        typeSize.isAccessibilitySize
            ? AnyLayout(VStackLayout(alignment: .leading, spacing: Theme.Space.s))
            : AnyLayout(HStackLayout(alignment: alignment, spacing: spacing))
    }

    var body: some View {
        let bus = kind == .bus
        let text = Color.cardText(line.route)
        let outline = Color.cardOutline(line.route)
        let layout = row(.top, Theme.Space.m)
        VStack(alignment: .leading, spacing: Theme.Space.s) {
            layout {
                VStack(alignment: .leading, spacing: 0) {
                    HStack(alignment: .top, spacing: Theme.Space.s) {
                        if kind == .bus {
                            Text(line.route)
                                .font(.system(size: routeSize, weight: .heavy, design: .rounded)).tracking(-1)
                                .lineLimit(1).minimumScaleFactor(0.5)
                        } else {
                            Text(stopName)
                                .font(.system(size: stationSize, weight: .heavy, design: .rounded))
                                .lineLimit(typeSize.isAccessibilitySize ? nil : 2).minimumScaleFactor(0.7)
                        }
                        if alert { GlyphIcon(image: Glyphs.alert, outline: outline).padding(.top, Theme.Space.m) }
                    }
                    Label {
                        Text(Self.heading(line, kind: kind, dest: next?.dest))
                    } icon: {
                        Text(Image(systemName: "arrow.right.circle.fill"))   // Text, so a bus card outlines it too
                    }
                    .font(Theme.rounded(.title2, .bold))
                    .labelStyle(TightLabel())
                    .lineLimit(2)
                    if kind == .bus {
                        Text(stopName).font(Theme.rounded(.headline, .semibold))
                            .padding(.top, 2)
                    }
                }
                if !typeSize.isAccessibilitySize { Spacer(minLength: 0) }
                ArrivalView(next: next, route: line.route, night: night, unknown: unknown, fresh: freshness == .fresh,
                            outline: outline, now: now)
                    .layoutPriority(1)
            }
            // Stacks under the footer at accessibility sizes, so the footer keeps its width.
            let bottom = row(.bottom, Theme.Space.s)
            bottom {
                footer
                if !typeSize.isAccessibilitySize { Spacer(minLength: 0) }
                // Which kind at a glance (Leo 2026-11-10): a subway in its tunnel, or a bus.
                Text(Image(systemName: bus ? "bus.fill" : "tram.fill.tunnel"))
                    .font(Theme.rounded(.title2, .bold))
            }
        }
        .foregroundStyle(text)
        .outlinedText(outline)
        .padding(Theme.Space.l)
        .frame(maxWidth: .infinity, alignment: .leading)
        .plate(Color.cardGradient(line.route), rim: outline ?? text.opacity(0.25), rimWidth: outline == nil ? 2 : 3)
        .contentShape(PixelShape())
        .opacity(freshness == .fresh ? 1 : 0.55)   // stale cards recede (app spec 8)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(CardSpeech.sentence(line: line, kind: kind, stopName: stopName, next: next, later: later,
                                                alert: alert, freshness: freshness, now: now, night: night))
    }

    @ViewBuilder private var footer: some View {
        let also = ArrivalFormat.also(later, now: now)
        let delayed = next?.delayed == true
        let aged: Int? = if case .aged(let m) = freshness { m } else { nil }
        let layout = row(.center, Theme.Space.s)
        if also != nil || delayed || aged != nil {
            layout {
                if delayed {
                    Text("Delayed").font(Theme.rounded(.caption, .bold))
                        .foregroundStyle(.black)
                        .padding(.horizontal, Theme.Space.s).padding(.vertical, Theme.Space.xs)
                        .background(Theme.amber, in: PixelShape.small)
                        .fixedSize()
                        .plainText()   // amber badges stay plain on a bus card
                }
                if let also { Text(also).font(Theme.rounded(.subheadline, .bold)) }
                if let aged { Text("updated \(aged) min ago").font(Theme.rounded(.subheadline, .semibold)) }
            }
        }
    }
}

extension RouteCard {
    /// A bus's compass direction ("Northbound", Leo 2026-11-10: clearer than its last stop); a train's destination.
    static func heading(_ line: Line, kind: Kind, dest: String?) -> String {
        kind == .bus ? line.dir : dest ?? line.dir
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
    var outline: Color? = nil
    @ScaledMetric(relativeTo: .footnote) private var points: CGFloat = 16
    var body: some View {
        PixelImage(image: image, points: points, template: true)
            .pixelOutline(outline, pixel: CGFloat(PixelScale.scale(art: image.width, points: points)))
    }
}

/// The big minutes in the pixel font, like a departure board, with "minutes" under it and the blinking light-green
/// live glyph at its corner. A scheduled arrival shows its clock time, smaller, with the glyph still and blue and a
/// SCHEDULED tag under it (Leo 2026-11-10).
private struct ArrivalView: View {
    let next: Prediction?
    let route: String
    let night: Bool
    let unknown: Bool
    let fresh: Bool
    var outline: Color? = nil
    let now: Date

    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    /// The big part and the small unit under it: ("3", "min"), ("Due", nil), or ("10:05", nil).
    private var parts: (big: String, unit: String?)? {
        guard let p = next else { return nil }
        let m = ArrivalFormat.minutes(until: p.at, now: now)
        return p.live && m >= 1 ? ("\(m)", m == 1 ? "minute" : "minutes") : (ArrivalFormat.label(p, now: now), nil)
    }

    var body: some View {
        if let parts {
            VStack(alignment: .center, spacing: 0) {
                HStack(alignment: .top, spacing: Theme.Space.xs) {
                    PixelText(parts.big, points: 49, shrink: parts.unit == nil ? 3 : 0, outline: outline)   // 7× the 7-pixel digits
                        .id(parts.big)
                        .transition(.push(from: .bottom))   // minutes tick over as they change
                    if next?.live == true {
                        LiveGlyph(blinks: fresh, tint: Theme.live, outline: Color.glyphHalo(route)).padding(.top, Theme.Space.s)
                    } else {
                        StillGlyph(halo: Color.glyphHalo(route)).padding(.top, Theme.Space.s)
                    }
                }
                if let unit = parts.unit {
                    Text(unit).font(Theme.rounded(.subheadline, .bold)).padding(.top, Theme.Space.xs)
                } else if next?.live == false {
                    ScheduledTag(route: route).padding(.top, Theme.Space.xs)
                }
            }
            // A short ease, not a spring: a spring's settling tail redrew the card for about a second per minute change.
            .animation(reduceMotion ? nil : .easeOut(duration: 0.25), value: parts.big)
        } else {
            Text(unknown ? "—" : night ? "No service now" : "No arrivals").font(Theme.rounded(.headline, .bold))
                .fixedSize()
        }
    }
}

/// Blinks in 2 frames once a second while the data is fresh; stays lit and still on a stale card or under Reduce Motion.
/// On a white bus card it wears a one-art-pixel gray halo, so the light green reads.
private struct LiveGlyph: View {
    let blinks: Bool
    let tint: Color
    var outline: Color? = nil
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @Environment(\.cardClocksPaused) private var paused   // the list under stop detail: hidden, so still
    @ScaledMetric(relativeTo: .title) private var points: CGFloat = 16

    var body: some View {
        // The art at a whole-number scale, as `PixelImage` draws it; a halo hangs one art pixel outside that frame.
        let art = Glyphs.live[0]
        let scale = CGFloat(PixelScale.scale(art: art.width, points: points))
        let halo: CGFloat = outline == nil ? 0 : 1
        BlinkLayer(frames: Glyphs.live.map { BlinkLayer.tinted($0, tint, outline: outline) }, blinks: blinks && !reduceMotion && !paused)
            .frame(width: (CGFloat(art.width) + 2 * halo) * scale, height: (CGFloat(art.height) + 2 * halo) * scale)
            .padding(-halo * scale)
    }
}

/// The live glyph's art held still in the scheduled blue, at the live glyph's size and halo.
struct StillGlyph: View {
    var halo: Color? = nil
    @ScaledMetric(relativeTo: .title) private var points: CGFloat = 16

    var body: some View {
        let art = Glyphs.live[0]
        PixelImage(image: art, points: points, template: true)
            .pixelOutline(halo, pixel: CGFloat(PixelScale.scale(art: art.width, points: points)))
            .foregroundStyle(Theme.scheduled)
            .accessibilityHidden(true)
    }
}

/// The SCHEDULED pixel tag: a timetable arrival, not a live one. In the card's colors, reversed; never outlined.
struct ScheduledTag: View {
    let route: String

    var body: some View {
        Text("SCHEDULED").font(Theme.rounded(.caption, .heavy))
            .foregroundStyle(Color.card(route))
            .plainText()
            .padding(.horizontal, Theme.Space.s).padding(.vertical, 3)
            .lineLimit(1).minimumScaleFactor(0.5)   // shrinks to fit a tile at accessibility sizes, never overflows
            .background(Color.cardText(route), in: PixelShape.small)
    }
}

/// Pixel-art frames that Core Animation flips on its own clock, lit the first half of each wall-clock second. As a
/// SwiftUI timeline, every blink of every card's glyph redrew its card, twice a second.
private struct BlinkLayer: UIViewRepresentable {
    let frames: [CGImage]
    let blinks: Bool

    func makeUIView(context: Context) -> BlinkView { BlinkView() }

    func updateUIView(_ view: BlinkView, context: Context) {
        view.show(frames, blinks: blinks)
    }

    final class BlinkView: UIView {
        private var frames: [CGImage] = []
        private var blinks = false

        override init(frame: CGRect) {
            super.init(frame: frame)
            isUserInteractionEnabled = false
            layer.magnificationFilter = .nearest   // whole art pixels, never smoothed
            // Core Animation drops the blink while the app is in the background, and nothing else brings it back.
            NotificationCenter.default.addObserver(self, selector: #selector(foregrounded),
                                                   name: UIApplication.willEnterForegroundNotification, object: nil)
        }

        @objc private func foregrounded() { restart() }

        @available(*, unavailable) required init?(coder: NSCoder) { fatalError("not used") }

        func show(_ frames: [CGImage], blinks: Bool) {
            guard frames != self.frames || blinks != self.blinks else { return }
            (self.frames, self.blinks) = (frames, blinks)
            restart()
        }

        // Back in a window (scrolled into view), the blink starts again in step.
        override func didMoveToWindow() {
            super.didMoveToWindow()
            restart()
        }

        private func restart() {
            layer.removeAnimation(forKey: "blink")
            layer.contents = frames.first
            guard blinks, frames.count == 2, window != nil else { return }
            let blink = CAKeyframeAnimation(keyPath: "contents")
            blink.values = frames
            blink.keyTimes = [0, 0.5, 1]
            blink.calculationMode = .discrete
            blink.duration = 1
            blink.repeatCount = .infinity
            blink.isRemovedOnCompletion = false
            blink.timeOffset = Date().timeIntervalSinceReferenceDate.truncatingRemainder(dividingBy: 1)
            layer.add(blink, forKey: "blink")
        }
    }

    /// The one-color art in `color`, as a template image takes the foreground color, and with `outline` a one-pixel
    /// border holding the art shifted a pixel each way in that color behind it (`pixelOutline`'s halo). Kept per art
    /// and colors.
    static func tinted(_ art: CGImage, _ color: Color, outline: Color? = nil) -> CGImage {
        // The cards' colors are fixed sRGB, so they resolve the same in any environment.
        let c = color.resolve(in: EnvironmentValues()), o = outline?.resolve(in: EnvironmentValues())
        let key = TintKey(art: ObjectIdentifier(art), color: [c.red, c.green, c.blue, c.opacity],
                          outline: o.map { [$0.red, $0.green, $0.blue, $0.opacity] })
        if let image = tints[key] { return image }
        let border = o == nil ? 0 : 1
        let (w, h) = (art.width + 2 * border, art.height + 2 * border)
        let ctx = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: 0,
                            space: CGColorSpace(name: CGColorSpace.sRGB)!,
                            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        func layer(_ color: Color.Resolved, at offsets: [(Int, Int)]) -> CGImage {
            let l = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: 0,
                              space: CGColorSpace(name: CGColorSpace.sRGB)!,
                              bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
            for (dx, dy) in offsets {
                l.draw(art, in: CGRect(x: border + dx, y: border + dy, width: art.width, height: art.height))
            }
            l.setBlendMode(.sourceIn)
            l.setFillColor(color.cgColor)
            l.fill(CGRect(x: 0, y: 0, width: w, height: h))
            return l.makeImage()!
        }
        let rect = CGRect(x: 0, y: 0, width: w, height: h)
        if let o { ctx.draw(layer(o, at: [(1, 0), (-1, 0), (0, 1), (0, -1)]), in: rect) }
        ctx.draw(layer(c, at: [(0, 0)]), in: rect)
        let image = ctx.makeImage()!
        tints[key] = image
        return image
    }

    private struct TintKey: Hashable {
        let art: ObjectIdentifier
        let color: [Float]
        let outline: [Float]?
    }

    private static var tints: [TintKey: CGImage] = [:]
}

// MARK: - Previews (every card state, light and dark, and the largest text size)

enum PreviewData {
    static let now = Date()
    static func p(_ route: String, _ dest: String, _ min: Double, live: Bool = true, delayed: Bool = false) -> Prediction {
        Prediction(stopID: "", route: route, dir: "", dest: dest, at: now.addingTimeInterval(min * 60 + 10),
                   live: live, delayed: delayed)
    }
    static let redN = Line(route: "Red", dir: "Howard")

    /// A row as the list draws it.
    @MainActor static func tile(_ card: RouteCard) -> some View { card }

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

#Preview("Rows") { PreviewData.cards.background(Theme.navy).preferredColorScheme(.dark) }
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
