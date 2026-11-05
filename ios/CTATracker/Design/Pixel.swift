import CTAKit
import SwiftUI

/// Pixel art at a whole-number scale with nearest-neighbor sampling, on whole points (app spec 4.6).
/// `template` draws it in the foreground style, for one-color glyphs on line colors.
struct PixelImage: View {
    let image: CGImage
    let points: CGFloat     // the art's width in points (sprites and glyphs are square)
    var template = false
    var scale: Int? = nil   // an exact scale instead, for wide art like pixel text

    var body: some View {
        let scale = scale ?? PixelScale.scale(art: image.width, points: points)
        Image(decorative: image, scale: 1)
            .renderingMode(template ? .template : .original)
            .resizable()
            .interpolation(.none)
            .frame(width: CGFloat(image.width * scale), height: CGFloat(image.height * scale))
    }
}

/// Text in the pixel font (the bitmap digits), drawn at the whole-number scale that fits `points` of height,
/// in the foreground style. Text without glyphs falls back to the snapped system monospaced font.
struct PixelText: View {
    let text: String
    @ScaledMetric private var points: CGFloat

    init(_ text: String, points: CGFloat = 40, relativeTo style: Font.TextStyle = .largeTitle) {
        self.text = text
        _points = ScaledMetric(wrappedValue: points, relativeTo: style)
    }

    var body: some View {
        if let grid = PixelFont.grid(text), let image = PetSprites.image(grid: grid, palette: [0, 0xFFFFFF]) {
            // Scaled by height, so "1" and "10:05" share one size.
            PixelImage(image: image, points: 0, template: true,
                       scale: PixelScale.scale(art: PixelFont.glyphHeight, points: points))
                .accessibilityLabel(text)
        } else {
            Text(text).pixelFont(points)
        }
    }
}

/// The pixel font's system fallback, snapped to its grid at every text size.
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

    // shortcut: a placeholder pet (sleepy) from the art brief in plan 1 task 11, until pet_sprites.json lands.
    static let pet: CGImage = PetSprites.grid([
        "................................",
        "................................",
        "................................",
        ".......KKKKKKKKKKKKKKKKKK.......",
        "......KBBBBBBBBBBBBBBBBBBK......",
        ".....KBBBBBBBBBBBBBBBBBBBBK.....",
        "....KBBBBBBBBBBBBBBBBBBBBBBK....",
        "....KBKKKKKKKKKBBKKKKKKKKKBK....",
        "....KBKBBBBBBBKBBKBBBBBBBKBK....",
        "....KBKBBBBBBBKBBKBBBBBBBKBK....",
        "....KBKBBBBBBBKBBKBBBBBBBKBK....",
        "....KBKBBBBBBBKBBKBBBBBBBKBK....",
        "....KBKKKKKKKKKBBKKKKKKKKKBK....",
        "....KBKWWEEEWWKBBKWWEEEWWKBK....",
        "....KBKWWWWWWWKBBKWWWWWWWKBK....",
        "....KBKWWWWWWWKBBKWWWWWWWKBK....",
        "....KBKKKKKKKKKBBKKKKKKKKKBK....",
        "....KBCCBBBBBBBBBBBBBBBBCCBK....",
        "....KBBBBBBBBBBBBBBBBBBBBBBK....",
        "....KSSSSSSSSSSSSSSSSSSSSSSK....",
        "....KSSSSSSSSSSSSSSSSSSSSSSK....",
        "....KBBBBBBBBBBBBBBBBBBBBBBK....",
        "....KBBBBBBBBBBBBBBBBBBBBBBK....",
        "....KBBBBBBBBKYYYYKBBBBBBBBK....",
        "....KBBBBBBBBKYYYYKBBBBBBBBK....",
        "....KBBBBBBKBBBBBBBBKBBBBBBK....",
        "....KBBBBBBBKKKKKKKKBBBBBBBK....",
        "....KBBBBBBBBBBBBBBBBBBBBBBK....",
        "....KKKKKKKKKKKKKKKKKKKKKKKK....",
        "................................",
        "................................",
        "................................"
    ], chars: ".KBSWEPCY").flatMap {
        PetSprites.image(grid: $0, palette: [0, 0x1A1A1A, 0xC0C6CC, 0x00A1DE, 0x2B3A55, 0xFFFFFF, 0x1A1A1A, 0xE27EA6, 0xF9E300])
    }!

    private static func glyph(_ rows: [String]) -> CGImage {
        PetSprites.grid(rows, chars: ".K").flatMap { PetSprites.image(grid: $0, palette: [0, 0xFFFFFF]) }!
    }
}
