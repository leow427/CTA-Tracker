import Foundation
import Testing
@testable import CTAKit

// Mirrors firmware test_alerts_weather (plan 1, task 6).
struct AlertsWeatherTests {
    @Test func alertRoutes() throws {
        let alerts = try AlertsWeather.alerts(fixture("alerts.json"))
        #expect(AlertsWeather.routes(alerts) == ["Red", "22"])   // station "41320" ignored
        #expect(alerts[0].headline == "Red Line trains are running with delays")
        #expect(alerts[0].fullText.contains("signal problem near Belmont"))
        let single = try AlertsWeather.alerts(fixture("alerts_single.json"))   // object, not array
        #expect(AlertsWeather.routes(single) == ["Brn"])
    }

    @Test func routesAreDistinct() {
        let a = ServiceAlert(id: "1", headline: "", shortText: "", fullText: "", routes: ["Red", "22"])
        let b = ServiceAlert(id: "2", headline: "", shortText: "", fullText: "", routes: ["Red"])
        #expect(AlertsWeather.routes([a, b]) == ["Red", "22"])
    }

    @Test func plainStringDescription() throws {
        let json = #"{"CTAAlerts":{"Alert":{"AlertId":"1","Headline":"H","FullDescription":"Plain text","ImpactedService":{"Service":{"ServiceType":"R","ServiceId":"Red"}}}}}"#
        let alerts = try AlertsWeather.alerts(Data(json.utf8))
        #expect(alerts[0].fullText == "Plain text")
    }

    @Test func weather() throws {
        let w = try AlertsWeather.weather(fixture("weather.json"))
        #expect(w.code == 61)
        #expect(w.tempC == -2.5)
    }

    @Test(arguments: [
        (61, 5.0, Accessory.umbrella),
        (61, -1.0, .umbrella),   // rain beats cold
        (95, 20.0, .umbrella),
        (71, 1.0, .scarf),
        (85, 0.0, .scarf),
        (3, -1.0, .scarf),
        (2, 10.0, .none),
    ])
    func accessory(code: Int, tempC: Double, expected: Accessory) {
        #expect(AlertsWeather.accessory(code: code, tempC: tempC) == expected)
    }
}
