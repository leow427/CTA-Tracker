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

    static let cardRadius: CGFloat = 16
    static let cardMargin: CGFloat = Space.l
    static let cardGap: CGFloat = Space.s
    static let mapButton: CGFloat = 44
    static let pinRim: CGFloat = 2

    /// The Delayed badge: the only saturated color besides the line colors.
    static let amber = Color(rgb: 0xFFB000)

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

/// Regular material, or a solid system background under Reduce Transparency.
struct MaterialBackground<S: Shape>: ViewModifier {
    let shape: S
    @Environment(\.accessibilityReduceTransparency) private var reduceTransparency

    func body(content: Content) -> some View {
        if reduceTransparency {
            content.background(Color(uiColor: .secondarySystemBackground), in: shape)
        } else {
            content.background(.regularMaterial, in: shape)
        }
    }
}

/// A soft, low shadow in light mode; none in dark, where cards sit on black.
struct CardShadow: ViewModifier {
    @Environment(\.colorScheme) private var scheme
    func body(content: Content) -> some View {
        content.shadow(color: .black.opacity(scheme == .dark ? 0 : 0.14), radius: 12, y: 6)
    }
}

/// The soft shadow under floating map controls.
struct FloatingShadow: ViewModifier {
    func body(content: Content) -> some View { content.shadow(color: .black.opacity(0.12), radius: 6, y: 2) }
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
    func cardShadow() -> some View { modifier(CardShadow()) }
    func floatingShadow() -> some View { modifier(FloatingShadow()) }

    func materialBackground<S: Shape>(_ shape: S) -> some View { modifier(MaterialBackground(shape: shape)) }
}
