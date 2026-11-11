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
    /// The live glyph off the line colors.
    static let signal = Color(rgb: 0x34C759)

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
nonisolated struct PixelShape: InsettableShape {
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
    let fill: AnyShapeStyle
    var rim = Color.white.opacity(0.22)
    var rimWidth: CGFloat = 2
    var edge: CGFloat = 5
    var shape = PixelShape()

    func body(content: Content) -> some View {
        content.background {
            ZStack {
                ZStack { shape.fill(fill); shape.fill(.black.opacity(0.45)) }.offset(y: edge)   // the plate's side
                shape.fill(fill)
                shape.fill(LinearGradient(colors: [.white.opacity(0.12), .clear], startPoint: .top, endPoint: .center))
                shape.strokeBorder(rim, lineWidth: rimWidth)
            }
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
    func plate(_ fill: some ShapeStyle = Theme.raised, rim: Color = .white.opacity(0.22), rimWidth: CGFloat = 2, edge: CGFloat = 5,
               shape: PixelShape = PixelShape()) -> some View {
        modifier(Plate(fill: AnyShapeStyle(fill), rim: rim, rimWidth: rimWidth, edge: edge, shape: shape))
    }
}

extension Color {
    /// A line's card fill and its text (redesign revision 2).
    static func card(_ route: String) -> Color { Color(rgb: LineColors.card(LineColors.accent(route))) }
    static func cardText(_ route: String) -> Color { Color(rgb: LineColors.cardText(LineColors.accent(route))) }
    /// A line's card fill: its lighter top shade down to its dark shade.
    static func cardGradient(_ route: String) -> LinearGradient {
        LinearGradient(colors: [Color(rgb: LineColors.cardTop(LineColors.accent(route))), card(route)],
                       startPoint: .top, endPoint: .bottom)
    }
}
