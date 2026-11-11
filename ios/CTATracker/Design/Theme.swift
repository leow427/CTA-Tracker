import CTAKit
import SwiftUI

/// The design kit's values (app spec 4.6). Screens use these, never one-off sizes or colors.
enum Theme {
    /// The 4 pt grid.
    enum Space {
        static let xs: CGFloat = 4
        static let s: CGFloat = 8
        static let m: CGFloat = 12
        static let l: CGFloat = 16
        static let xl: CGFloat = 24
        static let xxl: CGFloat = 32
    }

    static let cardMargin: CGFloat = Space.l
    static let cardGap: CGFloat = Space.m
    static let mapButton: CGFloat = 44
    static let pinRim: CGFloat = 2

    /// The Delayed badge.
    static let amber = Color(rgb: 0xFFB000)
    /// Night navy: the list panel and detail page, under a navy map (the app's identity).
    static let navy = Color(rgb: LineColors.navy)
    /// Raised surfaces on navy: chips, buttons, later departures.
    static let raised = Color(rgb: 0x17294F)
    /// Secondary text on navy (8:1).
    static let mist = Color(rgb: 0x9DB0D8)
    /// Hairlines between rows.
    static let hairline = Color.white.opacity(0.08)
    /// Headlight yellow, the pixel pet's yellow: the app's signature color (search bar, active controls).
    static let headlightRGB: UInt32 = 0xFFD95A
    static let headlight = Color(rgb: headlightRGB)
    /// The pet avatar's backdrop.
    static let creamRGB: UInt32 = 0xF4ECC8
    static let cream = Color(rgb: creamRGB)
    /// The live glyph's light green, on every card and tile (Leo 2026-11-10): 7.9:1 or better on every train card's
    /// dark shade; on white bus surfaces it takes a gray halo (`Color.glyphHalo`).
    static let live = Color(rgb: 0x7CF29A)
    /// A scheduled (timetable) arrival's still glyph: at least 4.49:1 on every train card (Pink's top shade is the lowest; a glyph needs 3:1), haloed in gray on white.
    static let scheduled = Color(rgb: 0x9AD0FF)

    /// SF Pro Rounded (redesign spec 5): heavy for route numbers and minutes, bold for destinations.
    static func rounded(_ style: Font.TextStyle, _ weight: Font.Weight) -> Font {
        .system(style, design: .rounded, weight: weight)
    }

    /// The card list rests with its top this far down the screen (redesign spec 2).
    static let listRest: CGFloat = 0.55
    /// Stop detail's page starts this far down, under the map framing the stop and its big route number.
    static let detailRest: CGFloat = 0.5
    /// The search bar on the navy panel's top edge.
    static let searchHeight: CGFloat = 58
    /// The pet avatar's tile at the top left, around 3× its 32-pixel art (96 pt).
    static let avatar: CGFloat = 108

    /// Secondary text: the app is always on navy.
    static let secondaryText = mist

    /// Map controls' symbols: a fixed size like Apple Maps' buttons, since the 44 pt circle doesn't grow.
    static let mapSymbol = Font.system(size: 17, weight: .semibold)

    /// The camera and sheet move on `.smooth`; selection on `.snappy`.
    static let smooth = Animation.smooth
    static let snappy = Animation.snappy
}

extension Color {
    init(rgb: UInt32) {
        self.init(.sRGB, red: Double(rgb >> 16 & 0xFF) / 255, green: Double(rgb >> 8 & 0xFF) / 255,
                  blue: Double(rgb & 0xFF) / 255)
    }
}

/// A rectangle whose corners step in whole blocks, like the pet's pixel art (redesign revision 2).
/// `steps` is how far each row of blocks at a corner is pulled in, in blocks; `bottom: false` squares the bottom.
nonisolated struct PixelShape: InsettableShape, Equatable {
    var block: CGFloat = 4
    var steps: [CGFloat] = [3, 1, 1]
    var bottom = true
    var inset: CGFloat = 0

    /// For chips and buttons.
    static let small = PixelShape(block: 3, steps: [2, 1])

    func path(in rect: CGRect) -> Path {
        let r = rect.insetBy(dx: inset, dy: inset)
        // The top-right corner, clockwise, as (in from the side, down from the top).
        var corner: [CGPoint] = []
        for (n, d) in steps.enumerated() {
            corner += [CGPoint(x: d * block, y: CGFloat(n) * block), CGPoint(x: d * block, y: CGFloat(n + 1) * block)]
        }
        corner.append(CGPoint(x: 0, y: CGFloat(steps.count) * block))
        let low = bottom ? corner : [.zero]
        let points = corner.map { CGPoint(x: r.maxX - $0.x, y: r.minY + $0.y) }
            + low.reversed().map { CGPoint(x: r.maxX - $0.x, y: r.maxY - $0.y) }
            + low.map { CGPoint(x: r.minX + $0.x, y: r.maxY - $0.y) }
            + corner.reversed().map { CGPoint(x: r.minX + $0.x, y: r.minY + $0.y) }
        var path = Path()
        path.addLines(points)
        path.closeSubpath()
        return path
    }

    func inset(by amount: CGFloat) -> PixelShape {
        var shape = self
        shape.inset += amount
        return shape
    }
}

/// A raised pixel plate, like a game tile: a solid fill lit from the top, a rim, and a hard darker edge under it,
/// so it sits up off the panel instead of lying flat.
struct Plate: ViewModifier {
    let fill: CardGradient
    var rim = Color.white.opacity(0.22)
    var rimWidth: CGFloat = 2
    var edge: CGFloat = 5
    var shape = PixelShape()

    func body(content: Content) -> some View {
        content
            .background {
                PlateLayers(fill: fill, rim: rim, rimWidth: rimWidth, edge: edge, shape: shape)
                    .padding(.bottom, -edge)   // the side hangs below the plate's frame
            }
            // Touches land on the plate and its side, as they did when it was drawn in SwiftUI shapes.
            .contentShape(PlateShape(shape: shape, edge: edge))
    }
}

/// A plate's face and its side below it, for hit testing.
nonisolated private struct PlateShape: Shape {
    let shape: PixelShape
    let edge: CGFloat

    func path(in rect: CGRect) -> Path {
        var path = shape.path(in: rect)
        path.addPath(shape.path(in: rect.offsetBy(dx: 0, dy: edge)))
        return path
    }
}

/// The plate as Core Animation layers, which the render server rasterizes once and keeps until the plate itself
/// changes. Drawn in SwiftUI with the card, its gradients were rasterized again on the CPU whenever anything on the
/// card changed: a blink, a minute, the list's clock.
private struct PlateLayers: UIViewRepresentable {
    let fill: CardGradient
    let rim: Color
    let rimWidth: CGFloat
    let edge: CGFloat
    let shape: PixelShape

    func makeUIView(context: Context) -> PlateView { PlateView() }

    func updateUIView(_ view: PlateView, context: Context) {
        // The kit's colors are fixed sRGB, so they resolve the same in any environment.
        func cg(_ color: Color) -> CGColor { color.resolve(in: EnvironmentValues()).cgColor }
        view.style = PlateView.Style(top: cg(fill.top), bottom: cg(fill.bottom), rim: cg(rim), rimWidth: rimWidth,
                                     edge: edge, shape: shape)
    }

    final class PlateView: UIView {
        struct Style: Equatable {
            let top, bottom, rim: CGColor
            let rimWidth, edge: CGFloat
            let shape: PixelShape
        }

        var style: Style? {
            didSet { if style != oldValue { setNeedsLayout() } }
        }

        // The side (the fill, darkened), then the face (the fill, lit from the top), then the rim, as SwiftUI drew them.
        private let side = CALayer(), sideFill = CAGradientLayer(), sideShade = CAShapeLayer(), sideMask = CAShapeLayer()
        private let face = CALayer(), faceFill = CAGradientLayer(), light = CAGradientLayer(), faceMask = CAShapeLayer()
        private let rimLayer = CAShapeLayer()

        override init(frame: CGRect) {
            super.init(frame: frame)
            isUserInteractionEnabled = false
            side.mask = sideMask
            side.addSublayer(sideFill)
            side.addSublayer(sideShade)
            face.mask = faceMask
            face.addSublayer(faceFill)
            face.addSublayer(light)
            for l in [side, face, rimLayer] { layer.addSublayer(l) }
            sideShade.fillColor = UIColor.black.withAlphaComponent(0.45).cgColor
            light.colors = [UIColor.white.withAlphaComponent(0.12).cgColor, UIColor.white.withAlphaComponent(0).cgColor]
            light.endPoint = CGPoint(x: 0.5, y: 0.5)   // fades out by the plate's middle
            rimLayer.fillColor = nil
            layer.shouldRasterize = true   // composited once, reused until it changes
        }

        @available(*, unavailable) required init?(coder: NSCoder) { fatalError("not used") }

        override func layoutSubviews() {
            super.layoutSubviews()
            guard let style else { return }
            CATransaction.begin()
            CATransaction.setDisableActions(true)
            let scale = window?.screen.scale ?? traitCollection.displayScale
            layer.rasterizationScale = scale
            let plate = CGRect(x: 0, y: 0, width: bounds.width, height: max(0, bounds.height - style.edge))
            let path = style.shape.path(in: plate).cgPath
            side.frame = plate.offsetBy(dx: 0, dy: style.edge)
            face.frame = plate
            rimLayer.frame = plate
            for l in [sideFill, sideShade, sideMask, faceFill, light, faceMask] as [CALayer] { l.frame = plate }
            for l in [side, face, rimLayer, sideFill, sideShade, sideMask, faceFill, light, faceMask] as [CALayer] {
                l.contentsScale = scale
            }
            sideMask.path = path
            sideShade.path = path
            faceMask.path = path
            sideFill.colors = [style.top, style.bottom]
            faceFill.colors = [style.top, style.bottom]
            // strokeBorder: a stroke inside the edge.
            rimLayer.path = style.shape.inset(by: style.rimWidth / 2).path(in: plate).cgPath
            rimLayer.lineWidth = style.rimWidth
            rimLayer.strokeColor = style.rim
            CATransaction.commit()
        }

        override func didMoveToWindow() {
            super.didMoveToWindow()
            setNeedsLayout()   // the screen's scale
        }
    }
}

/// Cards press down the instant a finger lands (on touch-down), on a snappy spring.
struct PressableCard: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .scaleEffect(configuration.isPressed ? 0.97 : 1)
            .animation(Theme.snappy, value: configuration.isPressed)
    }
}

extension View {
    func plate(_ fill: Color = Theme.raised, rim: Color = .white.opacity(0.22), rimWidth: CGFloat = 2, edge: CGFloat = 5,
               shape: PixelShape = PixelShape()) -> some View {
        plate(CardGradient(top: fill, bottom: fill), rim: rim, rimWidth: rimWidth, edge: edge, shape: shape)
    }

    func plate(_ fill: CardGradient, rim: Color = .white.opacity(0.22), rimWidth: CGFloat = 2, edge: CGFloat = 5,
               shape: PixelShape = PixelShape()) -> some View {
        modifier(Plate(fill: fill, rim: rim, rimWidth: rimWidth, edge: edge, shape: shape))
    }
}

extension Color {
    /// A line's card fill and its text (redesign revision 2); a bus's are white and gray (Leo 2026-11-10).
    static func card(_ route: String) -> Color {
        isBus(route) ? Theme.busCard.bottom : Color(rgb: LineColors.card(LineColors.accent(route)))
    }
    static func cardText(_ route: String) -> Color {
        isBus(route) ? Theme.busText : Color(rgb: LineColors.cardText(LineColors.accent(route)))
    }
    /// A line's card fill: its lighter top shade down to its dark shade.
    static func cardGradient(_ route: String) -> CardGradient {
        isBus(route) ? Theme.busCard : CardGradient(top: Color(rgb: LineColors.cardTop(LineColors.accent(route))), bottom: card(route))
    }
    /// The light blue that outlines a bus's text, icons and plates; trains have none.
    static func cardOutline(_ route: String) -> Color? { isBus(route) ? Theme.busOutline : nil }
    /// The halo around the light green and blue glyphs on a white bus surface (its gray text); trains need none.
    static func glyphHalo(_ route: String) -> Color? { isBus(route) ? Theme.busText : nil }

    private static func isBus(_ route: String) -> Bool { LineColors.train(route) == nil }
}

/// Text with a solid outline (bus cards): the text drawn shifted 8 ways in the outline color, then itself on top.
/// 2 pt on big text, 1 pt under about 20 pt, where a 2 pt halo crowded small gray letters to 3.3:1 (Leo 2026-11-11).
struct OutlinedText: TextRenderer {
    let color: Color
    var width: CGFloat = 2

    var displayPadding: EdgeInsets { EdgeInsets(top: width, leading: width, bottom: width, trailing: width) }

    /// A line's outline: thin below a 20 pt font, whose ascent plus descent is about 24 pt.
    private func width(_ line: Text.Layout.Line) -> CGFloat {
        let b = line.typographicBounds
        return b.ascent + b.descent < 24 ? min(1, width) : width
    }

    func draw(layout: Text.Layout, in ctx: inout GraphicsContext) {
        let bounds = layout.reduce(CGRect.null) { $0.union($1.typographicBounds.rect) }
            .insetBy(dx: -2 * width, dy: -2 * width)
        ctx.drawLayer { halo in
            for step in 0..<8 {
                let angle = Double(step) * .pi / 4
                for line in layout {
                    let w = width(line)
                    var shifted = halo
                    shifted.translateBy(x: cos(angle) * w, y: sin(angle) * w)
                    shifted.draw(line)
                }
            }
            halo.blendMode = .sourceIn   // every shifted copy becomes the outline color
            halo.fill(Path(bounds), with: .color(color))
        }
        for line in layout { ctx.draw(line) }
    }
}

private struct OutlinedTextModifier: ViewModifier {
    let color: Color?
    func body(content: Content) -> some View {
        if let color { content.textRenderer(OutlinedText(color: color)) } else { content }
    }
}

/// Text drawn as it is, for text inside an outlined view that must stay plain (amber badges, banners, alert text).
struct PlainText: TextRenderer {
    func draw(layout: Text.Layout, in ctx: inout GraphicsContext) {
        for line in layout { ctx.draw(line) }
    }
}

extension View {
    /// Every Text inside outlined in `color` (a bus's light blue); nil leaves text as it is.
    func outlinedText(_ color: Color?) -> some View { modifier(OutlinedTextModifier(color: color)) }

    /// Text inside drawn plainly, even within `outlinedText`.
    func plainText() -> some View { textRenderer(PlainText()) }
}

/// A plate's fill, from its top color down to its bottom one (the same for a solid plate).
struct CardGradient: Equatable {
    let top: Color
    let bottom: Color
}

extension Theme {
    /// Bus cards (Leo 2026-11-10): white, gray text, with the text, icons and card outlined in light blue.
    static let busCard = CardGradient(top: Color(rgb: 0xFFFFFF), bottom: Color(rgb: 0xEEF3F7))
    static let busText = Color(rgb: 0x5B6573)   // 5.29:1 on the card's bottom, 5.91:1 on white
    static let busOutline = Color(rgb: 0x7FCBF2)
}
