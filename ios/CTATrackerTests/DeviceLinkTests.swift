import Foundation
import Network
import Security
import Testing
@testable import CTATracker

// The link client's decisions, without the network (ecosystem plan Task 10, spec 4.3 and 7).
struct DeviceLinkTests {
    let dock = DeviceLink.Device(id: "a1b2", kind: .dock, name: "Dock", lastIP: "192.168.1.20:80")

    @Test func bonjourFirstThenLastIP() {
        #expect(DeviceLink.address(of: "a1b2", found: ["a1b2": "192.168.1.30:80"], known: [dock]) == "192.168.1.30:80")
        #expect(DeviceLink.address(of: "a1b2", found: [:], known: [dock]) == "192.168.1.20:80")   // mDNS blocked
        #expect(DeviceLink.address(of: "f4ce", found: ["f4ce": "10.0.0.5:18080"], known: [dock]) == "10.0.0.5:18080")
        #expect(DeviceLink.address(of: "c3d4", found: [:], known: [dock]) == nil)
    }

    @Test func readsCarryNoTokenWritesNeedOne() throws {
        let read = try DeviceLink.request("GET", "/info", address: "192.168.1.20:80", token: "ab")
        #expect(read.url?.absoluteString == "http://192.168.1.20:80/info")
        #expect(read.value(forHTTPHeaderField: "Authorization") == nil)

        let token = String(repeating: "0f", count: 16)
        let write = try DeviceLink.request("POST", "/pin", address: "192.168.1.20:80", token: token, body: Data("{}".utf8))
        #expect(write.httpMethod == "POST")
        #expect(write.value(forHTTPHeaderField: "Authorization") == "Bearer " + token)
        #expect(write.value(forHTTPHeaderField: "Content-Type") == "application/json")
        #expect(write.url?.query == nil)   // the token never rides in the URL

        #expect(throws: DeviceLink.LinkError.self) { try DeviceLink.request("DELETE", "/pin", address: "1.2.3.4:80", token: nil) }
        #expect(throws: DeviceLink.LinkError.self) { try DeviceLink.request("GET", "/info", address: nil, token: nil) }
    }

    @Test func resolvedEndpointIsIPv4AndPort() {
        let v4 = NWEndpoint.hostPort(host: .ipv4(IPv4Address("192.168.1.5")!), port: 18080)
        #expect(DeviceLink.address(v4) == "192.168.1.5:18080")
        #expect(DeviceLink.address(.hostPort(host: .ipv6(.loopback), port: 80)) == nil)
        #expect(DeviceLink.address(.service(name: "cta-dock-f4ce", type: "_ctatracker._tcp", domain: "local", interface: nil)) == nil)
    }

    @Test func tokenRoundTripsThroughKeychain() {
        let saved = TokenStore.token()
        // Never leave this public test token behind for setup to hand to the boards.
        defer {
            if let saved { TokenStore.setToken(saved) } else { SecItemDelete(TokenStore.query as CFDictionary) }
            #expect(TokenStore.token() == saved)
        }
        let token = String(repeating: "a5", count: 16)
        #expect(TokenStore.setToken(token))
        #expect(TokenStore.token() == token)
        #expect(TokenStore.setToken(String(repeating: "5a", count: 16)))   // replaces, doesn't duplicate
        #expect(TokenStore.token() == String(repeating: "5a", count: 16))
    }
}
