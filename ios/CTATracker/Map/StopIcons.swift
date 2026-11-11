import CTAKit
import MapboxMaps
import SwiftUI

/// The map's stop icons (redesign revision 3): a 16×16 pixel train front in its line's color for a station, or a
/// cream one with a strip of line-color pixels for a station on several lines, and a pixel bus in the bus blue.
/// Each gets a 1-pixel white halo so dark lines (Brown, Purple) read on the navy map.
enum StopIcons {
    /// The icon's style image id for a stop: "bus", or "st-" plus its lines in `LineColors.trains` order.
    static func id(_ stop: Stop) -> String {
        stop.kind == .bus ? "bus" : "st-" + routes(stop).joined(separator: "-")
    }

    /// A station's lines, in the CTA's order.
    static func routes(_ stop: Stop) -> [String] {
        let served = Set((stop.platforms ?? []).flatMap(\.lines).map(\.route))
        return LineColors.trains.map(\.route).filter(served.contains)
    }

    /// One style image per distinct icon among `stops`, with its id (StyleImage hides its own). Each is drawn once
    /// and kept, so the map body (rebuilt as the list scrolls) hands Mapbox the same images every pass.
    static func images(_ stops: [Stop], displayScale: CGFloat) -> [(id: String, image: StyleImage)] {
        var seen = Set<String>()
        return stops.compactMap { stop in
            let id = id(stop)
            guard seen.insert(id).inserted else { return nil }
            let key = "\(id)@\(displayScale)"
            if cache[key] == nil { cache[key] = image(stop, displayScale: displayScale).map { StyleImage(id: id, image: $0) } }
            return cache[key].map { (id, $0) }
        }
    }

    private static var cache: [String: StyleImage] = [:]

    // MARK: - Drawing

    /// The icon's target size; each art pixel is a whole number of device pixels, so it lands at or under this
    /// (18 art pixels at 5 px on 3× is 30 pt; at 3 px on 2×, 27 pt).
    private static let points: CGFloat = 30

    private static let chars = ".KCDWYH"   // clear, outline, body, glass, glint, headlight, halo
    private static let glass: UInt32 = 0x1E3460, white: UInt32 = 0xFFFFFF

    private static let train = [
        "....KKKKKKKK....",
        "..KKCCCCCCCCKK..",
        ".KCCCCCCCCCCCCK.",
        ".KCDDDDDDDDDDCK.",
        ".KCDWDDDDDDDDCK.",
        ".KCDDDDDDDDDDCK.",
        ".KCDDDDDDDDDDCK.",
        ".KCCCCCCCCCCCCK.",
        ".KCCCCCCCCCCCCK.",
        ".KCYYCCCCCCYYCK.",
        ".KCCCCCCCCCCCCK.",
        ".KKKKKKKKKKKKKK.",
        "...KK......KK...",
        "..KKKKKKKKKKKK..",
        "................",
        "................",
    ]

    private static let bus = [
        ".KKKKKKKKKKKKKK.",
        "KCCCCCCCCCCCCCCK",
        "KCYYYYYYYYYYYYCK",
        "KCCCCCCCCCCCCCCK",
        "KCDDDDDDDDDDDDCK",
        "KCDWDDDDDDDDDDCK",
        "KCDDDDDDDDDDDDCK",
        "KCDDDDDDDDDDDDCK",
        "KCCCCCCCCCCCCCCK",
        "KCCCCCCCCCCCCCCK",
        "KCYYCCCCCCCCYYCK",
        "KCCCCCCCCCCCCCCK",
        "KKKKKKKKKKKKKKKK",
        ".KKK........KKK.",
        "................",
        "................",
    ]

    private static func image(_ stop: Stop, displayScale: CGFloat) -> UIImage? {
        let lines = stop.kind == .bus ? [] : routes(stop)
        let body = stop.kind == .bus ? LineColors.busAccent : lines.count == 1 ? LineColors.rgb(lines[0]) : Theme.creamRGB
        var palette: [UInt32] = [0, LineColors.navy, body, glass, white, Theme.headlightRGB, white]
        guard var grid = PetSprites.grid(stop.kind == .bus ? bus : train, chars: chars) else { return nil }
        if lines.count > 1 {
            // The rail becomes a strip: an outlined bar with 2 pixels per line, centered.
            let shown = Array(lines.prefix(6))
            grid[13] = Array(repeating: 1, count: 16); grid[13][0] = 0; grid[13][15] = 0
            grid[14] = grid[13]
            grid[15] = grid[13]
            let start = 8 - shown.count
            for (n, route) in shown.enumerated() {
                palette.append(LineColors.rgb(route))
                let index = UInt8(palette.count - 1)
                grid[14][start + 2 * n] = index
                grid[14][start + 2 * n + 1] = index
            }
        }
        guard let art = PetSprites.image(grid: haloed(grid), palette: palette) else { return nil }
        return upscaled(art, displayScale: max(1, displayScale))
    }

    /// The grid with a 1-pixel border, where every clear cell touching a drawn one becomes halo (index 6).
    private static func haloed(_ grid: [[UInt8]]) -> [[UInt8]] {
        let w = grid[0].count + 2, h = grid.count + 2
        var out = Array(repeating: Array(repeating: UInt8(0), count: w), count: h)
        for (y, row) in grid.enumerated() { for (x, c) in row.enumerated() { out[y + 1][x + 1] = c } }
        let drawn = out
        for y in 0..<h {
            for x in 0..<w where drawn[y][x] == 0 {
                let near = [(0, 1), (0, -1), (1, 0), (-1, 0)].contains { dy, dx in
                    let (ny, nx) = (y + dy, x + dx)
                    return ny >= 0 && ny < h && nx >= 0 && nx < w && drawn[ny][nx] != 0
                }
                if near { out[y][x] = 6 }
            }
        }
        return out
    }

    /// Nearest-neighbor upscale to a whole number of device pixels per art pixel, tagged with the screen's scale,
    /// so Mapbox draws it 1:1 and never smooths the art.
    private static func upscaled(_ art: CGImage, displayScale: CGFloat) -> UIImage? {
        let pixel = max(1, Int(points * displayScale / CGFloat(art.width)))
        let (w, h) = (art.width * pixel, art.height * pixel)
        guard let ctx = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: 0,
                                  space: CGColorSpace(name: CGColorSpace.sRGB)!,
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        ctx.interpolationQuality = .none
        ctx.draw(art, in: CGRect(x: 0, y: 0, width: w, height: h))
        return ctx.makeImage().map { UIImage(cgImage: $0, scale: displayScale, orientation: .up) }
    }
}
