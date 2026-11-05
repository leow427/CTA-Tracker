import CoreGraphics
import Foundation

/// Pixel art draws at whole-number scales only (app spec 4.6).
public enum PixelScale {
    /// The largest whole scale at which `art` pixels fit in `points`, never below 1.
    public static func scale(art: Int, points: Double) -> Int {
        max(1, Int((points / Double(art)).rounded(.down)))
    }
}

/// The pixel font for the big minutes and the pet label (app spec 4.6): a 5×7 departure-board face drawn as
/// index grids, rendered through `PetSprites` at whole-number scales. Text it has no glyphs for falls back to
/// system monospaced, snapped to `unit`.
/// shortcut: our own stand-in glyphs (0–9, ":", "Due"); swap in the devices' pixel font when it's chosen with the final art.
public enum PixelFont {
    public static let unit: Double = 2
    public static let glyphHeight = 7

    public static func snap(_ points: Double) -> Double {
        max(unit, (points / unit).rounded(.down) * unit)
    }

    public static let glyphs: [Character: [String]] = [
        "0": [".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."],
        "1": ["..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."],
        "2": [".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"],
        "3": ["####.", "....#", "....#", ".###.", "....#", "....#", "####."],
        "4": ["...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."],
        "5": ["#####", "#....", "####.", "....#", "....#", "#...#", ".###."],
        "6": [".###.", "#....", "#....", "####.", "#...#", "#...#", ".###."],
        "7": ["#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."],
        "8": [".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."],
        "9": [".###.", "#...#", "#...#", ".####", "....#", "....#", ".###."],
        ":": [".", ".", "#", ".", "#", ".", "."],
        "D": ["####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."],
        "u": [".....", ".....", "#...#", "#...#", "#...#", "#..##", ".##.#"],
        "e": [".....", ".....", ".###.", "#...#", "#####", "#....", ".###."],
    ]

    /// One index grid for the whole text (1 = ink), glyphs one column apart; nil if any character has no glyph.
    public static func grid(_ text: String) -> [[UInt8]]? {
        guard !text.isEmpty else { return nil }
        var rows = Array(repeating: [UInt8](), count: glyphHeight)
        for (n, char) in text.enumerated() {
            guard let glyph = glyphs[char] else { return nil }
            for r in 0..<glyphHeight {
                if n > 0 { rows[r].append(0) }
                rows[r] += glyph[r].map { $0 == "#" ? 1 : 0 }
            }
        }
        return rows
    }
}

/// Index grids plus a palette, rendered to images. Palette slot 0 is transparent.
public enum PetSprites {
    /// One row per string, one palette index per character (its position in `chars`); nil on an unknown character.
    public static func grid(_ rows: [String], chars: String) -> [[UInt8]]? {
        let map = Dictionary(uniqueKeysWithValues: chars.enumerated().map { ($1, UInt8($0)) })
        var out: [[UInt8]] = []
        for row in rows {
            var r: [UInt8] = []
            for c in row {
                guard let i = map[c] else { return nil }
                r.append(i)
            }
            out.append(r)
        }
        return out
    }

    /// An sRGB image of the grid, one pixel per cell; nil if the grid is empty, ragged, or indexes past the palette.
    public static func image(grid: [[UInt8]], palette: [UInt32]) -> CGImage? {
        guard let width = grid.first?.count, width > 0, grid.allSatisfy({ $0.count == width }),
              grid.joined().allSatisfy({ Int($0) < palette.count }) else { return nil }
        var bytes: [UInt8] = []
        bytes.reserveCapacity(width * grid.count * 4)
        for i in grid.joined() {
            let rgb = palette[Int(i)]
            bytes += i == 0 ? [0, 0, 0, 0] : [UInt8(rgb >> 16 & 0xFF), UInt8(rgb >> 8 & 0xFF), UInt8(rgb & 0xFF), 0xFF]
        }
        guard let provider = CGDataProvider(data: Data(bytes) as CFData) else { return nil }
        return CGImage(width: width, height: grid.count, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: width * 4,
                       space: CGColorSpace(name: CGColorSpace.sRGB)!,
                       bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.last.rawValue),
                       provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)
    }
}
