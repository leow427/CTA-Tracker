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
    var shrink = 0   // whole pixel scales to step down, for tight spots (ViewThatFits tries 0 first)
    @ScaledMetric private var points: CGFloat

    init(_ text: String, points: CGFloat = 40, relativeTo style: Font.TextStyle = .largeTitle, shrink: Int = 0) {
        self.text = text
        self.shrink = shrink
        _points = ScaledMetric(wrappedValue: points, relativeTo: style)
    }

    var body: some View {
        if let grid = PixelFont.grid(text), let image = PetSprites.image(grid: grid, palette: [0, 0xFFFFFF]) {
            // Scaled by height, so "1" and "10:05" share one size.
            PixelImage(image: image, points: 0, template: true,
                       scale: max(1, PixelScale.scale(art: PixelFont.glyphHeight, points: points) - shrink))
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

    /// Leo's train pet (32×32): one body, a face per mood drawn on its door, and a little extra that floats beside
    /// it (heart, excitement lines, sweat drop, Zz; sad has its tears on its face).
    static func pet(_ mood: Mood) -> CGImage { pets[mood]! }
    static func petExtra(_ mood: Mood) -> CGImage? { extras[mood] }

    private static let pets = Dictionary(uniqueKeysWithValues: Mood.allCases.map { ($0, petImage(face: petFaces[$0]!)) })
    private static let extras = petExtras.mapValues { petArt($0) }

    private static let petChars = ".KBLGSWDCRYPUT"
    private static let petPalette: [UInt32] = [0, 0x0B0D14, 0x2D6BE6, 0x7DB4FF, 0xC3C8CE, 0x8E949B, 0xFFFFFF, 0x5A6069,
                                               0x9BEFF5, 0xE5333D, 0xFFD43B, 0xF27DAE, 0xA9AEB5, 0x7FD3FF]
    private static let petBody = [
        "................................",
        "..........KKKKKKKKKKKK..........",
        ".......KBBLLLLLLLLLLLLBBK.......",
        ".....KBWLLLLKKKKKKKKLLLLBBK.....",
        "....KBWBBBBBKCCCCCCKBBBBBBBK....",
        "...KBBBBBBBBKCCCCCCKBBBBBBBBK...",
        "..KBBBKKKBBBKKKKKKKKBBBKKKBBBK..",
        ".KGGBKKKKKBBBBBBBBBBBBKKKKKBGGK.",
        ".KWGBKRWKKBBKKKKKKKKBBKKWRKBGSK.",
        ".KWGBKKKKKBKKKKKKKKKKBKKKKKBGSK.",
        ".KWGBKKKKKBKKKKKKKKKKBKKKKKBGSK.",
        ".KWGBKKKKKBKKKKKKKKKKBKKKKKBGSK.",
        ".KWGBKKKKKBKKKKKKKKKKBKKKKKBGSK.",
        ".KWGBKKKKKBKKKKKKKKKKBKKKKKBGSK.",
        ".KWGBKKKKKBKKKKKKKKKKBKKKKKBGSK.",
        ".KWGBKKKKKBKKKKKKKKKKBKKKKKBGSK.",
        ".KWGBKUUUKBKKKKKKKKKKBKUUUKBGSK.",
        ".KWGBKKKKKBKKKKKKKKKKBKKKKKBGSK.",
        ".KWGBKKKKKBKKKKKKKKKKBKKKKKBGSK.",
        ".KWGBBKKKBBKKKKKKKKKKBBKKKBBGSK.",
        ".KWGBYYBWWBKKKKKKKKKKBWWBYYBGSK.",
        ".KWGBYYBWWBKKKKKKKKKKBWWBYYBGSK.",
        ".KWGBBBBBBBKKKKKKKKKKBBBBBBBGSK.",
        ".KWGGBBBBBBBBBBBBBBBBBBBBBBGGSK.",
        ".KWGGGGGGGGGGGGGGGGGGGGGGGGGGSK.",
        ".KGGGGGGGGGGGGGGGGGGGGGGGGGGGGK.",
        "..KKKKKKKKKKKKKKKKKKKKKKKKKKKK..",
        "...UUUU.DD...UUUUUU...DD.UUUU...",
        "...UUUU.DD...UUUUUU...DD.UUUU...",
        "........DD...UUUUUU...DD........",
        "........DD............DD........",
        "................................"
    ]
    /// Each face covers the door's rows 12–19, columns 11–20; "." keeps the door's black.
    private static let petFaces: [Mood: [String]] = [
        .happy: ["..........",
            "..........",
            ".WW....WW.",
            "W..W..W..W",
            "..........",
            "PP.W..W.PP",
            "....WW....",
            ".........."],
        .hyped: ["..........",
            "WW......WW",
            "..W....W..",
            "WW......WW",
            "..........",
            "PP.WWWW.PP",
            "....PP....",
            ".........."],
        .worried: ["..W....W..",
            "WW......WW",
            "..........",
            ".WW....WW.",
            ".WW....WW.",
            "..........",
            "...W.W.W..",
            "..W.W.W..."],
        .sad: ["..........",
            "..........",
            "WWW....WWW",
            ".T......T.",
            ".T......T.",
            ".T..WW..T.",
            ".T.W..W.T.",
            ".........."],
        .sleepy: ["..........",
            "..........",
            "..........",
            "WWW....WWW",
            "..........",
            "..........",
            "....WW....",
            ".........."]
    ]
    private static let petExtras: [Mood: [String]] = [
        .happy: [".PP.PP.", "PPPPPPP", "PPPPPPP", ".PPPPP.", "..PPP..", "...P...", "......."],
        .hyped: ["....WW.", "..WW...", ".......", "..WWWW.", ".......", "..WW...", "....WW."],
        .worried: ["...T...", "..TT...", "..TT...", ".TTTT..", ".TTWT..", "..TT...", "......."],
        .sleepy: ["WWWW...", "..W....", ".W.....", "WWWW...", "....WWW", ".....W.", "....WWW"]
    ]

    private static func petImage(face: [String]) -> CGImage {
        var rows = petBody.map(Array.init)
        for (dy, row) in face.enumerated() {
            for (dx, c) in row.enumerated() where c != "." { rows[12 + dy][11 + dx] = c }
        }
        return petArt(rows.map { String($0) })
    }

    private static func petArt(_ rows: [String]) -> CGImage {
        PetSprites.grid(rows, chars: petChars).flatMap { PetSprites.image(grid: $0, palette: petPalette) }!
    }

    private static func glyph(_ rows: [String]) -> CGImage {
        PetSprites.grid(rows, chars: ".K").flatMap { PetSprites.image(grid: $0, palette: [0, 0xFFFFFF]) }!
    }
}
