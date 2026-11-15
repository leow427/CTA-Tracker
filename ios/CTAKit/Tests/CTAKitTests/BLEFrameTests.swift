import Foundation
import Testing
@testable import CTAKit

// Mirrors firmware test_ble_frame (wearable BLE link plan, L1): byte 0 = first 0x80 | last 0x40 | seq 0–63.
struct BLEFrameTests {
    func bytes(_ b: [UInt8]) -> Data { Data(b) }

    @Test func singleChunkHasBothBits() throws {
        let chunks = try #require(BLEFrames.split(Data("hi".utf8), chunkMax: 20))
        #expect(chunks == [bytes([0xC0, 0x68, 0x69])])
        var r = BLEFrames.Reassembler()
        #expect(r.add(chunks[0]) == Data("hi".utf8))
    }

    @Test func threeChunksReassemble() throws {
        // chunkMax 4 = 3 payload bytes per chunk
        let chunks = try #require(BLEFrames.split(Data("abcdefgh".utf8), chunkMax: 4))
        #expect(chunks == [bytes([0x80, 0x61, 0x62, 0x63]), bytes([0x01, 0x64, 0x65, 0x66]), bytes([0x42, 0x67, 0x68])])
        var r = BLEFrames.Reassembler()
        #expect(r.add(chunks[0]) == nil)
        #expect(r.add(chunks[1]) == nil)
        #expect(r.add(chunks[2]) == Data("abcdefgh".utf8))
    }

    @Test func gapDropsMessage() {
        var r = BLEFrames.Reassembler()
        #expect(r.add(bytes([0x80, 0x61])) == nil)
        #expect(r.add(bytes([0x42, 0x63])) == nil)   // seq 2 after 0
        #expect(r.add(bytes([0x41, 0x62])) == nil)   // the message is gone, so seq 1 is now an orphan
    }

    @Test func duplicateDropsMessage() {
        var r = BLEFrames.Reassembler()
        #expect(r.add(bytes([0x80, 0x61])) == nil)
        #expect(r.add(bytes([0x01, 0x62])) == nil)
        #expect(r.add(bytes([0x41, 0x62])) == nil)   // seq 1 again
        #expect(r.add(bytes([0x42, 0x63])) == nil)
    }

    @Test func continuationFirstIsError() {
        var r = BLEFrames.Reassembler()
        #expect(r.add(bytes([0x41, 0x61])) == nil)
        #expect(r.add(bytes([0x00, 0x61])) == nil)
        #expect(r.add(Data()) == nil)
        #expect(r.add(bytes([0xC0, 0x7A])) == Data("z".utf8))   // still works after
    }

    @Test func firstChunkWithNonzeroSeqIsError() {
        var r = BLEFrames.Reassembler()
        #expect(r.add(bytes([0xC5, 0x61])) == nil)
    }

    @Test func overflowAtMaxPlusOne() throws {
        let max = Data(repeating: 0x61, count: BLEFrames.maxMessage)
        #expect(BLEFrames.split(max + Data([0x61]), chunkMax: 509) == nil)
        let chunks = try #require(BLEFrames.split(max, chunkMax: 509))
        var r = BLEFrames.Reassembler()
        var out: Data?
        for c in chunks { out = r.add(c) }
        #expect(out == max)

        // A sender that ignores the limit: the receiver drops the message at byte 8193.
        var big = BLEFrames.Reassembler()
        #expect(big.add(Data([0x80]) + Data(repeating: 0x61, count: 4096)) == nil)
        #expect(big.add(Data([0x01]) + Data(repeating: 0x61, count: 4096)) == nil)
        #expect(big.add(Data([0x42, 0x61])) == nil)
    }

    @Test func sequenceWrapsAfter64() throws {
        // 65 chunks of 1 payload byte: seq 0…63, then 0 again.
        let msg = Data((0..<65).map { UInt8($0) })
        let chunks = try #require(BLEFrames.split(msg, chunkMax: 2))
        #expect(chunks.count == 65)
        #expect(chunks[0][0] == 0x80 && chunks[63][0] == 0x3F && chunks[64][0] == 0x40)
        var r = BLEFrames.Reassembler()
        var out: Data?
        for c in chunks { out = r.add(c) }
        #expect(out == msg)
    }

    @Test func firstChunkResetsHalfMessage() {
        var r = BLEFrames.Reassembler()
        #expect(r.add(bytes([0x80, 0x61])) == nil)
        #expect(r.add(bytes([0x80, 0x62])) == nil)
        #expect(r.add(bytes([0x41, 0x63])) == Data("bc".utf8))
    }

    @Test func splitThenReassembleRoundTrips() throws {
        let msg = Data((0..<3000).map { UInt8($0 % 251) })
        for mtu in [20, 185, 509] {
            let chunks = try #require(BLEFrames.split(msg, chunkMax: mtu - 3))
            #expect(chunks.allSatisfy { $0.count <= mtu - 3 })
            var r = BLEFrames.Reassembler()
            var out: Data?
            for c in chunks { out = r.add(c) }
            #expect(out == msg, "MTU \(mtu)")
        }
    }

    @Test func splitRejectsTinyChunks() {
        #expect(BLEFrames.split(Data("a".utf8), chunkMax: 1) == nil)
    }

    @Test func envelopePostPin() {
        #expect(BLEFrames.envelope("POST", "/pin", Data(#"{"id":"1"}"#.utf8)) == Data("POST /pin\n{\"id\":\"1\"}".utf8))
    }

    @Test func envelopeDeleteNoBody() {
        #expect(BLEFrames.envelope("DELETE", "/follow", nil) == Data("DELETE /follow\n".utf8))
    }
}
