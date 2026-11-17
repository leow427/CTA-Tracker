/// The map's stop icon grids (F7), pixel versions of the cards' SF Symbols: `tram.fill.tunnel` for a station and
/// `bus.fill` for a bus stop. The app colors and scales them (`StopIcons`); they live here so the tests reach them.
public enum StopArt {
    /// Palette slots: clear, outline, body, glass, glint, headlight, halo, tunnel arch. A station on several lines
    /// adds one slot per line from 8.
    public static let chars = ".KCDWYHT"

    /// Tunnel A: a one-pixel arch, a navy gap, and the tram front inside. The tram alone (F6's vehicles) is
    /// rows 2–13, columns 3–12, with its two `T` corners cleared.
    public static let station = [
        "....KKKKKKKK....",
        "..KKTTTTTTTTKK..",
        ".KTTKKKKKKKKTTK.",
        ".KTKKCCCCCCKKTK.",
        "KTKKCCDDDDCCKKTK",
        "KTKKCCCCCCCCKKTK",
        "KTKKCDDDDDDCKKTK",
        "KTKKCDWDDDDCKKTK",
        "KTKKCDDDDDDCKKTK",
        "KTKKCCCCCCCCKKTK",
        "KTKKCYCCCCYCKKTK",
        "KTKKCCCCCCCCKKTK",
        "KTKKKCKKKKCKKKTK",
        "KKKKCKKKKKKCKKKK",
        "................",
        "................",
    ]

    /// Bus A: a narrower body, side mirrors, a big windshield, two headlights with a navy grille, dark tires.
    public static let bus = [
        "...KKKKKKKKKK...",
        "..KCCCCCCCCCCK..",
        "KKKCDDDDDDDDCKKK",
        "KCKCDWDDDDDDCKCK",
        "KCKCDDDDDDDDCKCK",
        "KCKCDDDDDDDDCKCK",
        "KKKCDDDDDDDDCKKK",
        "..KCDDDDDDDDCK..",
        "..KCCCCCCCCCCK..",
        "..KCYCKKKKCYCK..",
        "..KCCCCCCCCCCK..",
        "..KKKKKKKKKKKK..",
        "..KKK......KKK..",
        "..KKK......KKK..",
        "................",
        "................",
    ]

    /// F6's vehicles: a round token, so a vehicle never reads as a (square) stop. A pixel disc (`K` ring, `C` fill)
    /// with the vehicle glyph (`W`) inside, its windows cut through to the fill: a tram with a rounded roof and
    /// splayed legs, a bus with a flat roof and wheels. The app adds the halo (`haloed`, 15 × 15).
    public static let trainToken = [
        "....KKKKK....",
        "..KKCCCCCKK..",
        ".KCCWWWWWCCK.",
        ".KCWCCCCCWCK.",
        "KCCWCCCCCWCCK",
        "KCCWCCCCCWCCK",
        "KCCWWWWWWWCCK",
        "KCCWYWWWYWCCK",
        "KCCWWWWWWWCCK",
        ".KCCWCCCWCCK.",
        ".KCWCCCCCWCK.",
        "..KKCCCCCKK..",
        "....KKKKK....",
    ]
    public static let busToken = [
        "....KKKKK....",
        "..KKCCCCCKK..",
        ".KCWWWWWWWCK.",
        ".KCWCCCCCWCK.",
        "KCCWCCCCCWCCK",
        "KCCWCCCCCWCCK",
        "KCCWCCCCCWCCK",
        "KCCWWWWWWWCCK",
        "KCCWYWWWYWCCK",
        ".KCWWWWWWWCK.",
        ".KCWWCCCWWCK.",
        "..KKCCCCCKK..",
        "....KKKKK....",
    ]

    /// The arch's cells in order: up the left side, over the top, down the right side.
    static let archPath: [(y: Int, x: Int)] = {
        let cells = station.enumerated().flatMap { y, row in
            row.enumerated().filter { $0.element == "T" }.map { (y: y, x: $0.offset) }
        }
        let left = cells.filter { $0.x < 8 }.sorted { ($0.y, -$0.x) > ($1.y, -$1.x) }
        let right = cells.filter { $0.x >= 8 }.sorted { ($0.y, $0.x) < ($1.y, $1.x) }
        return left + right
    }()

    /// The station grid. With several lines (M3) the arch splits into one even run per line, left to right in
    /// the given order, line n in slot 8 + n; one line keeps the arch in slot 7.
    public static func stationGrid(lines: Int) -> [[UInt8]] {
        var grid = PetSprites.grid(station, chars: chars)!
        guard lines > 1 else { return grid }
        for (i, cell) in archPath.enumerated() { grid[cell.y][cell.x] = UInt8(8 + i * lines / archPath.count) }
        return grid
    }

    /// The grid with a 1-pixel border, where every clear cell touching a drawn one becomes halo (slot 6).
    public static func haloed(_ grid: [[UInt8]]) -> [[UInt8]] {
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
}
