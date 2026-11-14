import Testing
@testable import CTAKit

// F7: the map's station and bus icons (tunnel A, M3, bus A).
struct StopArtTests {
    private let arch: UInt8 = 7   // "T" in StopArt.chars

    @Test func gridsAre16By16InTheStopCharset() throws {
        for rows in [StopArt.station, StopArt.bus] {
            let grid = try #require(PetSprites.grid(rows, chars: StopArt.chars))
            #expect(grid.count == 16 && grid.allSatisfy { $0.count == 16 })
        }
    }

    @Test func oneLineKeepsTheArchInItsSlot() {
        #expect(StopArt.stationGrid(lines: 1) == PetSprites.grid(StopArt.station, chars: StopArt.chars))
    }

    @Test func archSplitsIntoOneRunPerLine() throws {
        let plain = try #require(PetSprites.grid(StopArt.station, chars: StopArt.chars))
        let archCells = plain.joined().filter { $0 == arch }.count
        for lines in 2...6 {
            let grid = StopArt.stationGrid(lines: lines)
            // Only arch pixels change, and every one of them belongs to a line.
            for (row, plainRow) in zip(grid, plain) {
                for (c, p) in zip(row, plainRow) {
                    if p == arch { #expect((8..<8 + UInt8(lines)).contains(c)) } else { #expect(c == p) }
                }
            }
            // The first line sits on the left foot, the last on the right foot.
            #expect(grid[12][1] == 8 && grid[12][14] == 7 + UInt8(lines))
            // Walking the arch from the left foot over the top to the right foot meets lines 0, 1, 2… once each.
            let walk = StopArt.archPath.map { grid[$0.y][$0.x] }
            #expect(walk.count == archCells)
            var runs: [UInt8] = []
            for c in walk where runs.last != c { runs.append(c) }
            #expect(runs == (0..<UInt8(lines)).map { 8 + $0 }, "\(lines) lines")
            // Runs are even: lengths differ by at most one.
            let lengths = runs.map { r in walk.filter { $0 == r }.count }
            #expect(lengths.max()! - lengths.min()! <= 1)
        }
    }

    @Test func archPathIsConnected() {
        for (a, b) in zip(StopArt.archPath, StopArt.archPath.dropFirst()) {
            #expect(abs(a.x - b.x) <= 1 && abs(a.y - b.y) <= 1, "\(a) → \(b)")
        }
    }

    @Test func heightIsTheSameForAnyLineCount() {
        func drawnRows(_ g: [[UInt8]]) -> [Int] { g.indices.filter { g[$0].contains { $0 != 0 } } }
        let one = drawnRows(StopArt.haloed(StopArt.stationGrid(lines: 1)))
        for lines in 2...6 { #expect(drawnRows(StopArt.haloed(StopArt.stationGrid(lines: lines))) == one) }
    }

    @Test func haloRingsTheArtInSlot6() {
        let out = StopArt.haloed([[1]])
        #expect(out == [[0, 6, 0], [6, 1, 6], [0, 6, 0]])
    }
}
