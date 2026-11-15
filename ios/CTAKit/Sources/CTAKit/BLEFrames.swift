import Foundation

/// The wearable link's chunking (wearable BLE link plan, "Frames"). Mirrors firmware/lib/cta/src/ble_frame.*.
/// Byte 0: bit 7 first chunk, bit 6 last chunk, bits 0–5 a sequence that starts at 0 and wraps at 64.
public enum BLEFrames {
    public static let maxMessage = 8192   // kMaxBleMsg
    static let first: UInt8 = 0x80, last: UInt8 = 0x40, seqMask: UInt8 = 0x3F

    /// Chunks of at most `chunkMax` bytes, header included (pass the peripheral's maximum write length,
    /// ATT MTU − 3). Nil if the message is over `maxMessage` or `chunkMax` < 2.
    public static func split(_ message: Data, chunkMax: Int) -> [Data]? {
        guard message.count <= maxMessage, chunkMax >= 2 else { return nil }
        let bytes = [UInt8](message), step = chunkMax - 1
        let starts = Array(stride(from: 0, to: max(bytes.count, 1), by: step))
        return starts.enumerated().map { i, start in
            var header = UInt8(i & Int(seqMask))
            if i == 0 { header |= first }
            if i == starts.count - 1 { header |= last }
            return Data([header] + bytes[start..<min(start + step, bytes.count)])
        }
    }

    /// Joins chunks. A first chunk resets it; a gap, a duplicate, a continuation with no first chunk, or more than
    /// `maxMessage` bytes drops the message (status notifies are idempotent, so the next one fixes it).
    public struct Reassembler {
        private var buf = Data()
        private var next: UInt8? = nil   // the sequence expected next; nil = no message in progress
        public init() {}

        /// The whole message when this chunk was its last, else nil.
        public mutating func add(_ chunk: Data) -> Data? {
            guard let header = chunk.first else { return drop() }
            let seq = header & BLEFrames.seqMask
            if header & BLEFrames.first != 0 {
                guard seq == 0 else { return drop() }
                buf = Data()
            } else if seq != next {
                return drop()
            }
            buf.append(chunk.dropFirst())
            guard buf.count <= BLEFrames.maxMessage else { return drop() }
            if header & BLEFrames.last != 0 {
                defer { buf = Data(); next = nil }
                return buf
            }
            next = (seq + 1) & BLEFrames.seqMask
            return nil
        }

        private mutating func drop() -> Data? { buf = Data(); next = nil; return nil }
    }

    /// `"POST /pin\n{…}"`: the HTTP link's method, path and body in one message. The newline is always there.
    public static func envelope(_ method: String, _ path: String, _ body: Data?) -> Data {
        Data("\(method) \(path)\n".utf8) + (body ?? Data())
    }
}
