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

    /// The Delayed badge: the only saturated color besides the line colors.
    static let amber = Color(rgb: 0xFFB000)

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

    func materialBackground<S: Shape>(_ shape: S) -> some View { modifier(MaterialBackground(shape: shape)) }
}
