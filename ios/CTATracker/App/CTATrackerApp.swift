import MapboxMaps
import SwiftUI

@main
struct CTATrackerApp: App {
    @State private var model: AppModel
    @State private var arrivals: ArrivalsStore
    /// Arrivals to the wearable over Bluetooth while the app is in the foreground (plan AB3).
    private let relay: WearableRelay

    init() {
        // The public token from Secrets.plist (never in Info.plist or git). Without one the map stays blank.
        if let token = Secrets().mapbox { MapboxOptions.accessToken = token }
        let model = AppModel(), arrivals = ArrivalsStore()
        _model = State(initialValue: model)
        _arrivals = State(initialValue: arrivals)
        relay = WearableRelay(link: model.devices, arrivals: arrivals, index: model.index)
    }

    var body: some Scene {
        WindowGroup {
            MapScreen()
                .environment(model)
                .environment(arrivals)
                .preferredColorScheme(.dark)   // the app lives on night navy
        }
    }
}
