import MapboxMaps
import SwiftUI

@main
struct CTATrackerApp: App {
    @State private var model = AppModel()
    @State private var arrivals = ArrivalsStore()

    init() {
        // The public token from Secrets.plist (never in Info.plist or git). Without one the map stays blank.
        if let token = Secrets().mapbox { MapboxOptions.accessToken = token }
    }

    var body: some Scene {
        WindowGroup {
            MapScreen()
                .environment(model)
                .environment(arrivals)
        }
    }
}
