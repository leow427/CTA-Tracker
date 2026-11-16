/// Trackie's color presets (ecosystem spec 5.2), the same table as `art/presets.txt`.
public enum Pet {
    public struct Preset: Hashable, Sendable {
        public let name: String
        public let colors: [UInt32]   // 0xRRGGBB for palette slots 2 and 3
    }

    public static let presets: [Preset] = [
        Preset(name: "Blue", colors: [0x2D6BE6, 0x7DB4FF]),
        Preset(name: "Green", colors: [0x1E9E50, 0x72D98F]),
        Preset(name: "Orange", colors: [0xF26B1D, 0xFFAE70]),
        Preset(name: "Purple", colors: [0x7B4BD8, 0xB79BFF]),
        Preset(name: "Pink", colors: [0xE2549C, 0xFF9FCD]),
        Preset(name: "Teal", colors: [0x12A0A8, 0x6BDCE0]),
    ]

    public static let defaultConfig = PetConfig(name: "Trackie", colors: presets[0].colors)
}
