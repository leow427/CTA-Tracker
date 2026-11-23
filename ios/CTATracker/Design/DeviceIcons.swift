import CTAKit
import SwiftUI

/// The dock from the front: a charcoal wedge (its top shows behind the sloped face), the screen's three stop cards
/// on the left, and three LED rows ending at a cyan your-stop dot. Drawn on a 100-unit square, decorative.
struct DockIcon: View {
    var size: CGFloat = 32

    var body: some View {
        Canvas { ctx, canvas in
            let u = canvas.width / 100
            func r(_ x: CGFloat, _ y: CGFloat, _ w: CGFloat, _ h: CGFloat) -> CGRect { CGRect(x: x * u, y: y * u, width: w * u, height: h * u) }
            let top = Path(roundedRect: r(14, 16, 72, 12), cornerRadius: 5 * u)
            ctx.fill(top, with: .color(Color(rgb: 0x34383F)))
            var face = Path()   // narrower at the top: the front slopes back
            face.move(to: CGPoint(x: 12 * u, y: 24 * u))
            face.addLine(to: CGPoint(x: 88 * u, y: 24 * u))
            face.addLine(to: CGPoint(x: 96 * u, y: 84 * u))
            face.addLine(to: CGPoint(x: 4 * u, y: 84 * u))
            face.closeSubpath()
            let body = face.strokedPath(StrokeStyle(lineWidth: 6 * u, lineJoin: .round)).union(face)
            ctx.fill(body, with: .color(Color(rgb: 0x1C1F24)))
            ctx.stroke(body, with: .color(.white.opacity(0.35)), lineWidth: max(1, 1.5 * u))
            ctx.fill(Path(roundedRect: r(14, 32, 34, 44), cornerRadius: 3 * u), with: .color(.black))
            for (i, rgb) in [UInt32(0xC60C30), 0x2F7BFF, LineColors.bus].enumerated() {
                ctx.fill(Path(roundedRect: r(17, 35 + CGFloat(i) * 13.5, 28, 11.5), cornerRadius: 2 * u), with: .color(Color(rgb: rgb)))
            }
            for row in 0..<3 {
                for col in 0..<4 {
                    let color = col == 3 ? Color(rgb: 0x2BD9F0) : Color.white.opacity(0.85)
                    ctx.fill(Path(ellipseIn: r(56 + CGFloat(col) * 9, 40 + CGFloat(row) * 12, 5, 5)), with: .color(color))
                }
            }
        }
        .frame(width: size, height: size)
        .accessibilityHidden(true)
    }
}

/// Trackie from the front: a navy rounded body hanging from a loop, two yellow buttons on top, and a light-gray
/// bezel round the square screen, which can show the pet's face. Drawn on a 100-unit square, decorative.
struct TrackieIcon: View {
    var size: CGFloat = 32
    var mood: Mood? = nil

    var body: some View {
        Canvas { ctx, canvas in
            let u = canvas.width / 100
            func r(_ x: CGFloat, _ y: CGFloat, _ w: CGFloat, _ h: CGFloat) -> CGRect { CGRect(x: x * u, y: y * u, width: w * u, height: h * u) }
            ctx.stroke(Path(ellipseIn: r(61, 4, 13, 13)), with: .color(Color(rgb: 0x2A4A8C)), lineWidth: 4.5 * u)
            for x in [24.0, 66.0] {
                ctx.fill(Path(roundedRect: r(x, 17, 10, 8), cornerRadius: 3 * u), with: .color(Theme.headlight))
            }
            let body = Path(roundedRect: r(8, 20, 84, 74), cornerRadius: 22 * u)
            ctx.fill(body, with: .color(Color(rgb: 0x2A4A8C)))
            ctx.stroke(body, with: .color(.white.opacity(0.3)), lineWidth: max(1, 1.5 * u))
            ctx.fill(Path(roundedRect: r(19, 30, 62, 56), cornerRadius: 13 * u), with: .color(Color(rgb: 0xC9CED6)))
            ctx.fill(Path(roundedRect: r(26, 37, 48, 42), cornerRadius: 8 * u), with: .color(Color(rgb: 0x23343D)))
        }
        .frame(width: size, height: size)
        .overlay {
            if let mood {
                PixelImage(image: Glyphs.pet(mood), points: size * 0.36).offset(y: size * 0.08)
            }
        }
        .accessibilityHidden(true)
    }
}

#Preview {
    HStack(spacing: 20) {
        DockIcon(size: 24); TrackieIcon(size: 24); DockIcon(size: 40); TrackieIcon(size: 40)
        DockIcon(size: 128); TrackieIcon(size: 128, mood: .happy)
    }
    .padding().background(Theme.navy)
}
