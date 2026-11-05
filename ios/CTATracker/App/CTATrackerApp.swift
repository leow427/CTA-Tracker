import SwiftUI

@main
struct CTATrackerApp: App {
    @State private var model = AppModel()
    @State private var arrivals = ArrivalsStore()

    var body: some Scene {
        WindowGroup {
            MapScreen()
                .environment(model)
                .environment(arrivals)
        }
    }
}
