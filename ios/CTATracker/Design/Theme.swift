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

    static let cardRadius: CGFloat = 22
    static let cardMargin: CGFloat = Space.l
    static let cardGap: CGFloat = Space.m
    /// A tile's visible bottom edge, and how far its face sinks when pressed (redesign spec 3).
    static let tileEdge: CGFloat = 4
    static let pressDepth: CGFloat = 3
    static let mapButton: CGFloat = 44
    static let pinRim: CGFloat = 2

    /// The Delayed badge.
    static let amber = Color(rgb: 0xFFB000)
    /// Bus tiles, dark backgrounds, and dark text (redesign spec 5).
    static let ink = Color(rgb: 0x1C2A4A)
    /// The light background behind the scrolled-up list.
    static let paper = Color(rgb: 0xF6F2EA)
    /// The live glyph off the tiles (redesign spec 5).
    static let signal = Color(rgb: 0x34C759)

    /// SF Pro Rounded (redesign spec 5): heavy for route numbers and minutes, bold for destinations.
    static func rounded(_ style: Font.TextStyle, _ weight: Font.Weight) -> Font {
        .system(style, design: .rounded, weight: weight)
    }

    /// The card list rests with its top this far down the screen (redesign spec 2).
    static let listRest: CGFloat = 0.55
    /// How far above the top controls the list's background starts fading in.
    static let listFade: CGFloat = 120
    /// Stop detail's page starts this far down, under the map framing the stop (redesign spec 4).
    static let detailRest: CGFloat = 0.4

    /// Secondary text that keeps 4.5:1 on the sheet in both modes (system `.secondary` is about 3.4:1 on white):
    /// 0x636366 in light, 0xAEAEB2 in dark.
    static let secondaryText = Color(uiColor: UIColor { $0.userInterfaceStyle == .dark
        ? UIColor(red: 0xAE / 255, green: 0xAE / 255, blue: 0xB2 / 255, alpha: 1)
        : UIColor(red: 0x63 / 255, green: 0x63 / 255, blue: 0x66 / 255, alpha: 1) })

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

/// Liquid Glass (redesign spec 5), or solid Paper or Ink under Reduce Transparency.
struct GlassBackground<S: Shape>: ViewModifier {
    let shape: S
    let interactive: Bool
    @Environment(\.accessibilityReduceTransparency) private var reduceTransparency
    @Environment(\.colorScheme) private var scheme

    func body(content: Content) -> some View {
        if reduceTransparency {
            content.background(scheme == .dark ? Theme.ink : Theme.paper, in: shape)
        } else {
            content.glassEffect(interactive ? .regular.interactive() : .regular, in: shape)
        }
    }
}

/// A thick tile (redesign spec 3): the content is the face; this adds the darker bottom edge, a top highlight,
/// a tight contact shadow and a soft one on the map, and sinks the face onto its edge while pressed.
struct TileLook: ViewModifier {
    let rgb: UInt32
    var radius: CGFloat = Theme.cardRadius
    var pressed = false

    func body(content: Content) -> some View {
        let shape = RoundedRectangle(cornerRadius: radius, style: .continuous)
        content
            .clipShape(shape)
            .overlay {
                shape.strokeBorder(LinearGradient(colors: [.white.opacity(0.25), .clear],
                                                  startPoint: .top, endPoint: UnitPoint(x: 0.5, y: 0.3)), lineWidth: 1)
            }
            .shadow(color: .black.opacity(pressed ? 0.1 : 0.18), radius: pressed ? 1 : 2, y: pressed ? 1 : 2)
            .offset(y: pressed ? Theme.pressDepth : 0)
            .background { shape.fill(Color(rgb: LineColors.edge(rgb))).offset(y: Theme.tileEdge) }
            .padding(.bottom, Theme.tileEdge)
            .shadow(color: .black.opacity(0.12), radius: 16, y: 10)
    }
}

/// A tile as a button: it sinks while pressed, with a light impact.
struct TileStyle: ButtonStyle {
    let rgb: UInt32
    var radius: CGFloat = Theme.cardRadius

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .modifier(TileLook(rgb: rgb, radius: radius, pressed: configuration.isPressed))
            .animation(Theme.snappy, value: configuration.isPressed)
            .sensoryFeedback(.impact(weight: .light), trigger: configuration.isPressed) { _, now in now }
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

    func glassBackground<S: Shape>(_ shape: S, interactive: Bool = false) -> some View {
        modifier(GlassBackground(shape: shape, interactive: interactive))
    }
}
