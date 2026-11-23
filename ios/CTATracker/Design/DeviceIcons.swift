import CTAKit
import SwiftUI

/// The dock from the front: a black rounded body with a lighter top bevel, the portrait screen's three stop cards
/// on the left, three LED rows ending at a cyan your-stop dot, and the charging cradle with its red plug tab on the
/// right. Drawn on a 100-unit square, decorative.
struct DockIcon: View {
    var size: CGFloat = 32

    var body: some View {
        Canvas { ctx, canvas in
            let u = canvas.width / 100
            func r(_ x: CGFloat, _ y: CGFloat, _ w: CGFloat, _ h: CGFloat) -> CGRect { CGRect(x: x * u, y: y * u, width: w * u, height: h * u) }
            let rim = Color.white.opacity(0.35), line = max(1, 1.5 * u)
            ctx.fill(Path(roundedRect: r(85, 50, 4, 7), cornerRadius: 1 * u), with: .color(Color(rgb: 0xD8332F)))
            let cradle = Path(roundedRect: r(80, 56, 15, 20), cornerRadius: 3 * u)
            ctx.fill(cradle, with: .color(Color(rgb: 0x1C1F24)))
            ctx.stroke(cradle, with: .color(rim), lineWidth: line)
            let body = Path(roundedRect: r(3, 26, 80, 52), cornerRadius: 7 * u, style: .continuous)
            ctx.fill(body, with: .color(Color(rgb: 0x16181C)))
            ctx.fill(Path(roundedRect: r(6, 27.5, 74, 4), cornerRadius: 2 * u), with: .color(Color(rgb: 0x2E3238)))   // top bevel
            ctx.stroke(body, with: .color(rim), lineWidth: line)
            ctx.fill(Path(roundedRect: r(9, 33, 22, 39), cornerRadius: 2 * u), with: .color(.black))
            for (i, rgb) in [UInt32(0xC60C30), 0x2F7BFF, LineColors.bus].enumerated() {
                ctx.fill(Path(roundedRect: r(11, 35 + CGFloat(i) * 12, 18, 10.5), cornerRadius: 1.5 * u), with: .color(Color(rgb: rgb)))
            }
            let dots = size < 40 ? 4 : 6   // fewer, bigger dots when small, so the rows don't mush
            let step = 36 / CGFloat(dots - 1), d: CGFloat = dots == 4 ? 5 : 3.5
            for row in 0..<3 {
                for col in 0..<dots {
                    let color = col == dots - 1 ? Color(rgb: 0x2BD9F0) : Color.white.opacity(0.85)
                    ctx.fill(Path(ellipseIn: r(38 + CGFloat(col) * step, 40 + CGFloat(row) * 11.5, d, d)), with: .color(color))
                }
            }
        }
        .frame(width: size, height: size)
        .accessibilityHidden(true)
    }
}

/// Trackie face on: a black squircle hanging from a loop, a silver bezel plate with four corner screws, and a round
/// teal screen in a darker ring, which can show the pet's face. Drawn on a 100-unit square, decorative.
struct TrackieIcon: View {
    var size: CGFloat = 32
    var mood: Mood? = nil

    var body: some View {
        Canvas { ctx, canvas in
            let u = canvas.width / 100
            func r(_ x: CGFloat, _ y: CGFloat, _ w: CGFloat, _ h: CGFloat) -> CGRect { CGRect(x: x * u, y: y * u, width: w * u, height: h * u) }
            let rim = Color.white.opacity(0.35), black = Color(rgb: 0x16181C)
            let loop = Path(ellipseIn: r(41, 4, 18, 18))
            ctx.stroke(loop, with: .color(rim), lineWidth: 7.5 * u)
            ctx.stroke(loop, with: .color(black), lineWidth: 5 * u)
            let body = Path(roundedRect: r(9, 17, 82, 78), cornerRadius: 24 * u, style: .continuous)
            ctx.fill(body, with: .color(black))
            ctx.stroke(body, with: .color(rim), lineWidth: max(1, 1.5 * u))
            ctx.fill(Path(roundedRect: r(14, 22, 72, 68), cornerRadius: 19 * u, style: .continuous), with: .color(Color(rgb: 0xB9BDC5)))
            for (x, y) in [(23.0, 31.0), (77.0, 31.0), (23.0, 81.0), (77.0, 81.0)] {
                ctx.fill(Path(ellipseIn: r(x - 3, y - 3, 6, 6)), with: .color(black))
            }
            ctx.fill(Path(ellipseIn: r(21, 27, 58, 58)), with: .color(Color(rgb: 0x3D3F3A)))
            ctx.fill(Path(ellipseIn: r(25, 31, 50, 50)), with: .color(Color(rgb: 0x2F4D5C)))
        }
        .frame(width: size, height: size)
        .overlay {
            if let mood {
                PixelImage(image: Glyphs.pet(mood), points: size * 0.34).offset(y: size * 0.06)
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
