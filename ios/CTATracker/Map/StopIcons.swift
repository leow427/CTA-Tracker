import CTAKit
import MapboxMaps
import SwiftUI
import UIKit

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
        return upscaled(art, pixel: pixel(displayScale), displayScale: max(1, displayScale))
    }

    /// Device pixels per art pixel for the 18-pixel stop icons (5 on 3×, 3 on 2×); vehicles use the same.
    private static func pixel(_ displayScale: CGFloat) -> Int { max(1, Int(points * max(1, displayScale) / 18)) }

    // MARK: - Vehicles (F6)

    /// A vehicle's image id: "veh-" plus the train's route, or "veh-bus"; "-big" for the focused one.
    static func vehicleID(route: String, bus: Bool, big: Bool) -> String {
        "veh-" + (bus ? "bus" : route) + (big ? "-big" : "")
    }

    /// The open line's vehicle images (normal and focused) and the heading arrows on their rims. Each is a round
    /// token (`StopArt.trainToken`, `busToken`) with the stops' white halo, so it never reads as a square stop: a
    /// train's disc in its line color with a navy ring, a bus's like its cards (white, light-blue ring, gray bus). The
    /// focused one is drawn two device pixels bigger per art pixel, still whole and unsmoothed.
    static func vehicleImages(route: String, bus: Bool, displayScale: CGFloat) -> [(id: String, image: StyleImage)] {
        [false, true].flatMap { big in
            [(vehicleID(route: route, bus: bus, big: big), { vehicleArt(route: route, bus: bus, big: big, displayScale: displayScale) }),
             (headingID(big: big), { heading(big: big, displayScale: displayScale) })]
        }.compactMap { id, draw in
            let key = "\(id)@\(displayScale)"
            if cache[key] == nil { cache[key] = draw().map { StyleImage(id: id, image: $0) } }
            return cache[key].map { (id, $0) }
        }
    }

    static func headingID(big: Bool) -> String { big ? "heading-big" : "heading" }

    /// The vehicle's token at 1 px per art pixel, for the page's focus line.
    static func vehicleGlyph(route: String, bus: Bool) -> CGImage? {
        PetSprites.grid(bus ? StopArt.busToken : StopArt.trainToken, chars: StopArt.chars).flatMap {
            PetSprites.image(grid: StopArt.haloed($0), palette: vehiclePalette(route, bus: bus))
        }
    }

    /// Slots: clear, ring, disc, glass, glyph, headlight, halo. A train's glyph is white where it reads at 3:1 on the
    /// line color (WCAG non-text contrast), navy on Pink and Yellow.
    private static func vehiclePalette(_ route: String, bus: Bool) -> [UInt32] {
        if bus { return [0, Theme.busOutlineRGB, white, glass, Theme.busTextRGB, Theme.headlightRGB, white] }
        let line = LineColors.rgb(route)
        let glyph = LineColors.contrast(white, line) >= 3 ? white : LineColors.navy
        return [0, LineColors.navy, line, glass, glyph, Theme.headlightRGB, white]
    }

    /// Device pixels per art pixel for a vehicle token; the focused one two more.
    private static func vehiclePixel(_ displayScale: CGFloat, big: Bool) -> Int { pixel(displayScale) + (big ? 2 : 0) }

    private static func vehicleArt(route: String, bus: Bool, big: Bool, displayScale: CGFloat) -> UIImage? {
        vehicleGlyph(route: route, bus: bus).flatMap {
            upscaled($0, pixel: vehiclePixel(displayScale, big: big), displayScale: max(1, displayScale))
        }
    }

    /// A small white arrowhead with a navy edge at the top of a clear square, its base tucked 2 pt under the token's
    /// rim, so rotating the square by the vehicle's heading puts the arrow on the rim on the side it's heading (app
    /// spec 4.6: the pixel art itself is never rotated).
    private static func heading(big: Bool, displayScale: CGFloat) -> UIImage? {
        let scale = max(1, displayScale)
        let rim = CGFloat((StopArt.trainToken.count + 2) * vehiclePixel(displayScale, big: big)) / scale / 2
        let w: CGFloat = big ? 12 : 10, h: CGFloat = big ? 8 : 7, side = 2 * (rim - 2 + h + 1.5)
        let format = UIGraphicsImageRendererFormat()
        format.scale = scale
        return UIGraphicsImageRenderer(size: CGSize(width: side, height: side), format: format).image { ctx in
            let path = UIBezierPath()
            path.move(to: CGPoint(x: side / 2, y: 1.5))
            path.addLine(to: CGPoint(x: side / 2 + w / 2, y: 1.5 + h))
            path.addLine(to: CGPoint(x: side / 2 - w / 2, y: 1.5 + h))
            path.close()
            path.lineJoinStyle = .round
            path.lineWidth = 2
            UIColor(Color(rgb: LineColors.navy)).setStroke()
            UIColor.white.setFill()
            path.stroke()
            path.fill()
        }
    }

    /// Nearest-neighbor upscale to `pixel` device pixels per art pixel, tagged with the screen's scale, so Mapbox
    /// draws it 1:1 and never smooths the art.
    private static func upscaled(_ art: CGImage, pixel: Int, displayScale: CGFloat) -> UIImage? {
        let (w, h) = (art.width * pixel, art.height * pixel)
        guard let ctx = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: 0,
                                  space: CGColorSpace(name: CGColorSpace.sRGB)!,
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        ctx.interpolationQuality = .none
        ctx.draw(art, in: CGRect(x: 0, y: 0, width: w, height: h))
        return ctx.makeImage().map { UIImage(cgImage: $0, scale: displayScale, orientation: .up) }
    }
}
