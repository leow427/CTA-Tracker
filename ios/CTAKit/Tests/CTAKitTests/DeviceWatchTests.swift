import Foundation
import Testing
@testable import CTAKit

// What the app does after each `GET /info` poll of one device (ecosystem plan Review Focus 1, spec 5.9).
struct DeviceWatchTests {
    func info(_ type: DeviceKind = .handheld, uptime: Int = 100, picksVer: Int = 1, wake: Int = 1) throws -> InfoMessage {
        let json = """
        {"type":"\(type.rawValue)","id":"a1b2","fw":"0.4.0","battery":80,"charging":false,
         "wifi":{"connected":true,"ssid":"Net"},"last_update":0,"uptime":\(uptime),
         "picks_ver":\(picksVer),"wake":\(wake),"docked":false}
        """
        return try JSONDecoder().decode(InfoMessage.self, from: Data(json.utf8))
    }

    @Test func firstPollIsCameOnlinePlusResend() throws {
        var w = DeviceWatch()
        #expect(w.polled(try info()) == [.cameOnline, .resendState])
        #expect(w.polled(try info(uptime: 102)) == [])
        #expect(w.polled(try info(uptime: 102)) == [])   // same uptime is not a reboot

        var dock = DeviceWatch()   // a pick made before first contact gets pulled
        #expect(dock.polled(try info(.dock)) == [.cameOnline, .resendState, .fetchConfig])
        #expect(dock.polled(try info(.dock, uptime: 102)) == [])
    }

    @Test func picksVerUpFetchesConfig() throws {
        var w = DeviceWatch()
        _ = w.polled(try info(picksVer: 1))
        #expect(w.polled(try info(uptime: 102, picksVer: 2)) == [.fetchConfig])
        #expect(w.polled(try info(uptime: 104, picksVer: 2)) == [])
    }

    @Test func wakeUpSendsNearby() throws {
        var w = DeviceWatch()
        _ = w.polled(try info(wake: 7))
        #expect(w.polled(try info(uptime: 102, wake: 8)) == [.sendNearby])
        #expect(w.polled(try info(uptime: 104, wake: 8)) == [])
    }

    @Test func rebootResendsState() throws {
        var w = DeviceWatch()
        _ = w.polled(try info(uptime: 500, picksVer: 4, wake: 9))
        // Counters restart with the board; the resend already covers nearby.
        #expect(w.polled(try info(uptime: 3, picksVer: 4, wake: 0)) == [.resendState])
        #expect(w.polled(try info(uptime: 5, picksVer: 4, wake: 1)) == [.sendNearby])
    }

    @Test func rebootWithNewPicksAlsoFetches() throws {
        var w = DeviceWatch()
        _ = w.polled(try info(uptime: 500, picksVer: 4))
        #expect(w.polled(try info(uptime: 3, picksVer: 0)) == [.resendState, .fetchConfig])
    }

    @Test func threeMissesGoOffline() throws {
        var w = DeviceWatch()
        _ = w.polled(try info())
        #expect(w.polled(nil) == [])
        #expect(w.polled(nil) == [])
        #expect(w.polled(nil) == [.wentOffline])
        #expect(w.polled(nil) == [])
        #expect(w.polled(try info(uptime: 110)) == [.cameOnline, .resendState])

        var dock = DeviceWatch()
        _ = dock.polled(try info(.dock))
        for _ in 0..<3 { _ = dock.polled(nil) }
        #expect(dock.polled(try info(.dock, uptime: 110)) == [.cameOnline, .resendState, .fetchConfig])
    }

    @Test func aHitResetsTheMissCount() throws {
        var w = DeviceWatch()
        _ = w.polled(try info())
        _ = w.polled(nil); _ = w.polled(nil)
        _ = w.polled(try info(uptime: 106))
        #expect(w.polled(nil) == [] && w.polled(nil) == [])
        #expect(w.polled(nil) == [.wentOffline])
    }

    @Test func neverOnlineNeverGoesOffline() {
        var w = DeviceWatch()
        for _ in 0..<5 { #expect(w.polled(nil) == []) }
    }
}
