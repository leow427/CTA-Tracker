import CTAKit
import SwiftUI

/// Pixel art at a whole-number scale with nearest-neighbor sampling, on whole points (app spec 4.6).
/// `template` draws it in the foreground style, for one-color glyphs on line colors.
struct PixelImage: View {
    let image: CGImage
    let points: CGFloat
    var template = false

    var body: some View {
        let scale = PixelScale.scale(art: image.width, points: points)
        Image(decorative: image, scale: 1)
            .renderingMode(template ? .template : .original)
            .resizable()
            .interpolation(.none)
            .frame(width: CGFloat(image.width * scale), height: CGFloat(image.height * scale))
    }
}

/// The pixel font for the big minutes and the pet label, snapped to its grid at every text size.
struct PixelFontModifier: ViewModifier {
    @ScaledMetric private var points: CGFloat

    init(points: CGFloat, relativeTo style: Font.TextStyle) {
        _points = ScaledMetric(wrappedValue: points, relativeTo: style)
    }

    func body(content: Content) -> some View {
        // shortcut: system monospaced until the pixel font is chosen (plan 4 open question).
        content.font(.system(size: PixelFont.snap(points), weight: .bold, design: .monospaced))
    }
}

extension View {
    func pixelFont(_ points: CGFloat = 34, relativeTo style: Font.TextStyle = .largeTitle) -> some View {
        modifier(PixelFontModifier(points: points, relativeTo: style))
    }
}

/// The six glyphs (app spec 4.6), one-color, drawn as templates. Each is two frames where it animates.
enum Glyphs {
    // shortcut: placeholder grids until milestone 3 loads the drawn 16×16 glyphs from pet_sprites.json.
    static let live = [glyph([".......K", ".....K..", "..KK.K.K", ".KKKK.K.", ".KKKK.K.", "..KK.K.K", ".....K..", ".......K"]),
                       glyph(["........", "........", "..KK....", ".KKKK...", ".KKKK...", "..KK....", "........", "........"])]
    static let alert = glyph([".......KK.......", ".......KK.......", "......KKKK......", "......KKKK......",
                              ".....KKKKKK.....", ".....KK..KK.....", "....KKK..KKK....", "....KKK..KKK....",
                              "...KKKK..KKKK...", "...KKKK..KKKK...", "..KKKKKKKKKKKK..", "..KKKKKKKKKKKK..",
                              ".KKKKKK..KKKKKK.", ".KKKKKK..KKKKKK.", "KKKKKKKKKKKKKKKK", "KKKKKKKKKKKKKKKK"])

    private static func glyph(_ rows: [String]) -> CGImage {
        PetSprites.grid(rows, chars: ".K").flatMap { PetSprites.image(grid: $0, palette: [0, 0xFFFFFF]) }!
    }
}
