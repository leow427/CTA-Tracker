import Foundation

extension String {
    /// The longest prefix that fits in `maxBytes` of UTF-8 without splitting a character's bytes
    /// (plan 1 decision 3; mirrors the firmware's copyTruncated).
    func truncatedUTF8(_ maxBytes: Int) -> String {
        let bytes = Array(utf8)
        guard bytes.count > maxBytes else { return self }
        var end = maxBytes
        while end > 0 && bytes[end] & 0xC0 == 0x80 { end -= 1 }   // bytes[end] continues a cut sequence
        return String(decoding: bytes[..<end], as: UTF8.self)
    }
}
