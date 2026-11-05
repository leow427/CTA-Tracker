import CTAKit
import SwiftUI

/// Milestone 1 smoke screen: CTAKit's line colors, text contrast, and arrival labels.
/// Milestone 2 replaces it with the map and route sheet.
struct PlaceholderScreen: View {
    let now = Date()

    var body: some View {
        NavigationStack {
            List {
                ForEach(Array(LineColors.trains.enumerated()), id: \.offset) { i, line in
                    row(line.name, rgb: line.rgb, sample(minutesAway: i * 3, live: i % 3 != 2))
                }
                row("22 Clark", rgb: LineColors.bus, sample(minutesAway: 4, live: true))
            }
            .navigationTitle("CTA Tracker")
        }
    }

    private func sample(minutesAway m: Int, live: Bool) -> Prediction {
        Prediction(stopID: "", route: "", dir: "", dest: "", at: now.addingTimeInterval(Double(m * 60 + 10)),
                   live: live, delayed: false)
    }

    private func row(_ name: String, rgb: UInt32, _ p: Prediction) -> some View {
        HStack {
            Text(name).font(.headline)
            Spacer()
            Text(ArrivalFormat.label(p, now: now)).font(.title2.monospacedDigit().bold())
        }
        .foregroundStyle(Color(rgb: LineColors.textColor(on: rgb)))
        .listRowBackground(Color(rgb: rgb))
    }
}

extension Color {
    init(rgb: UInt32) {
        self.init(.sRGB, red: Double(rgb >> 16 & 0xFF) / 255, green: Double(rgb >> 8 & 0xFF) / 255,
                  blue: Double(rgb & 0xFF) / 255)
    }
}

#Preview { PlaceholderScreen() }
