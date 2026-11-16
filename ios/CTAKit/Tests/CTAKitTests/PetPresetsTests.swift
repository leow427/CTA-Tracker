import Foundation
import Testing
@testable import CTAKit

struct PetPresetsTests {
    @Test func matchesArtFile() throws {
        let url = URL(filePath: #filePath).deletingLastPathComponent().appending(path: "../../../../art/presets.txt").standardized
        let rows = try String(contentsOf: url, encoding: .utf8).split(separator: "\n")
            .filter { !$0.hasPrefix("#") && !$0.trimmingCharacters(in: .whitespaces).isEmpty }
            .map { $0.split(separator: " ") }
        let file = try rows.map { r in
            try #require(r.count == 3)
            let hex = try r[1...].map { try #require(UInt32($0.dropFirst(2), radix: 16)) }
            return Pet.Preset(name: String(r[0]), colors: hex)
        }
        #expect(file == Pet.presets)
        #expect(file.count == 6)
        #expect(file.first?.colors == [0x2D6BE6, 0x7DB4FF])   // today's blue stays the default
    }
}
