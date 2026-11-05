import SwiftUI

/// Checkpoint B smoke screen: every RouteCard state on the design kit.
/// Checkpoint C replaces it with the map and route sheet.
struct PlaceholderScreen: View {
    var body: some View {
        NavigationStack {
            PreviewData.cards.navigationTitle("CTA Tracker")
        }
    }
}

#Preview { PlaceholderScreen() }
