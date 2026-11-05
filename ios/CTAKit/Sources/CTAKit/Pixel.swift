import CoreGraphics
import Foundation

/// Pixel art draws at whole-number scales only (app spec 4.6).
public enum PixelScale {
    /// The largest whole scale at which `art` pixels fit in `points`, never below 1.
    public static func scale(art: Int, points: Double) -> Int {
        max(1, Int((points / Double(art)).rounded(.down)))
    }
}

/// The pixel font's sizes snap to whole multiples of its pixel grid at every text size.
public enum PixelFont {
    /// shortcut: the system monospaced stand-in has no pixel grid, so 2 pt; set the real font's unit when it's chosen.
    public static let unit: Double = 2

    public static func snap(_ points: Double) -> Double {
        max(unit, (points / unit).rounded(.down) * unit)
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
