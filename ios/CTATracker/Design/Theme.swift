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
    static let headlight = Color(rgb: 0xFFD95A)
    /// The pet avatar's backdrop.
    static let cream = Color(rgb: 0xF4ECC8)
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
    /// The navy panel's rounded top, and the search bar that sits on it.
    static let panelRadius: CGFloat = 28
    static let searchHeight: CGFloat = 58

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

/// A solid raised surface on navy with a faint rim: no glass, so it reads the same over any map.
struct Surface<S: InsettableShape>: ViewModifier {
    let shape: S
    var fill = Theme.raised

    func body(content: Content) -> some View {
        content
            .background(fill, in: shape)
            .overlay { shape.strokeBorder(Color.white.opacity(0.08), lineWidth: 1) }
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

    func surface<S: InsettableShape>(_ shape: S, fill: Color = Theme.raised) -> some View {
        modifier(Surface(shape: shape, fill: fill))
    }
}
