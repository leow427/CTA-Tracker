import CTAKit
import MapboxMaps
import SwiftUI

/// The map's stop icons (F7, grids in `StopArt`): a pixel tram in its tunnel in its line's color for a station, or a
/// cream tram whose arch is split into one run per line for a station on several lines, and a pixel bus in the bus
/// blue. Each gets a 1-pixel white halo so dark lines (Brown, Purple) read on the navy map.
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

    private static let glass: UInt32 = 0x1E3460, white: UInt32 = 0xFFFFFF

    private static func image(_ stop: Stop, displayScale: CGFloat) -> UIImage? {
        let lines = stop.kind == .bus ? [] : routes(stop)
        let body = stop.kind == .bus ? LineColors.busAccent : lines.count == 1 ? LineColors.rgb(lines[0]) : Theme.creamRGB
        // Slot 7 is the tunnel arch, in the line's color; with several lines each gets its own slot from 8.
        let palette: [UInt32] = [0, LineColors.navy, body, glass, white, Theme.headlightRGB, white, body]
            + lines.map(LineColors.rgb)
        guard let grid = stop.kind == .bus ? PetSprites.grid(StopArt.bus, chars: StopArt.chars)
                                           : StopArt.stationGrid(lines: lines.count),
              let art = PetSprites.image(grid: StopArt.haloed(grid), palette: palette) else { return nil }
        return upscaled(art, displayScale: max(1, displayScale))
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
