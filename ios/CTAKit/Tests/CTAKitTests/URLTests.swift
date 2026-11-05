import Foundation
import Testing
@testable import CTAKit

// Mirrors firmware test_urls (plan 1, task 9).
struct URLTests {
    @Test func trainArrivals() throws {
        let station = try #require(CTAClient.trainArrivalsURL(key: "K", id: "41320")).absoluteString
        #expect(station.hasPrefix("https://lapi.transitchicago.com/api/1.0/ttarrivals.aspx?"))
        #expect(station.contains("mapid=41320") && station.contains("outputType=JSON"))
        let platform = try #require(CTAClient.trainArrivalsURL(key: "K", id: "30255")).absoluteString
        #expect(platform.contains("stpid=30255"))
    }

    @Test func busPredictions() throws {
        #expect(CTAClient.busPredictionsURL(key: "K", stopIDs: (1...11).map(String.init)) == nil)
        #expect(CTAClient.busPredictionsURL(key: "K", stopIDs: []) == nil)
        let url = try #require(CTAClient.busPredictionsURL(key: "K", stopIDs: ["1001", "1002"])).absoluteString
        #expect(url.hasPrefix("https://www.ctabustracker.com/bustime/api/v2/getpredictions?"))
        #expect(url.contains("stpid=1001,1002&format=json"))
    }

    @Test func others() throws {
        #expect(try #require(CTAClient.trainPositionsURL(key: "K", route: "Red")).absoluteString.contains("ttpositions.aspx?key=K&rt=Red&outputType=JSON"))
        #expect(try #require(CTAClient.busVehiclesURL(key: "K", routes: ["22", "36"])).absoluteString.contains("getvehicles?key=K&rt=22,36&format=json"))
        #expect(CTAClient.busVehiclesURL(key: "K", routes: (1...11).map(String.init)) == nil)
        #expect(try #require(CTAClient.busPatternsURL(key: "K", route: "22")).absoluteString.contains("getpatterns?key=K&rt=22&format=json"))
        #expect(try #require(CTAClient.alertsURL(routes: ["Red", "22"])).absoluteString
            == "https://www.transitchicago.com/api/1.0/alerts.aspx?outputType=JSON&activeonly=true&routeid=Red,22")
        #expect(try #require(CTAClient.weatherURL(lat: 41.87812, lon: -87.62980)).absoluteString.contains("latitude=41.8781&longitude=-87.6298"))
    }
}
