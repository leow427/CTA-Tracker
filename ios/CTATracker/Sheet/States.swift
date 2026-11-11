import SwiftUI
import UIKit

/// An empty state: the 128 pt pixel pet over the spec's copy (app spec 4.6, 8).
struct EmptyState<Actions: View>: View {
    let title: String
    var message: String? = nil
    @ViewBuilder var actions: Actions

    var body: some View {
        VStack(spacing: Theme.Space.m) {
            PixelImage(image: Glyphs.pet(.sad), points: 128)
                .padding(.bottom, Theme.Space.s)
                .accessibilityHidden(true)
            Text(title)
                .font(Theme.rounded(.title3, .bold))
                .foregroundStyle(.white)
                .multilineTextAlignment(.center)
            if let message {
                Text(message)
                    .font(.subheadline)
                    .foregroundStyle(Theme.secondaryText)
                    .multilineTextAlignment(.center)
            }
            actions
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, Theme.Space.xxl)
        .padding(.horizontal, Theme.Space.l)
    }
}

extension EmptyState where Actions == EmptyView {
    init(title: String, message: String? = nil) {
        self.init(title: title, message: message) { EmptyView() }
    }
}

/// Location off (app spec 8): the map starts on the Loop, search and ★ still work.
struct LocationOff: View {
    @Environment(\.openURL) private var openURL

    var body: some View {
        EmptyState(title: "Turn on location to see stops near you") {
            Button("Open Settings") {
                if let url = URL(string: UIApplication.openSettingsURLString) { openURL(url) }
            }
            .buttonStyle(.borderedProminent)
            .buttonBorderShape(.capsule)
            .tint(Theme.headlight)
            .foregroundStyle(Theme.navy)
            .font(Theme.rounded(.headline, .bold))
            .padding(.top, Theme.Space.xs)
        }
    }
}

/// "CTA data unavailable" (app spec 8). The last cards stay, dimmed, with their age.
struct UnavailableBanner: View {
    var body: some View {
        Label("CTA data unavailable", systemImage: "wifi.exclamationmark")
            .font(.subheadline.weight(.semibold))
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(.horizontal, Theme.Space.l)
            .padding(.vertical, Theme.Space.m)
            .foregroundStyle(.white)
            .plainText()   // white on navy, even on a bus stop's outlined page
            .plate(edge: 4, shape: .small)
            .padding(.top, Theme.Space.m)
            .accessibilityAddTraits(.isStaticText)
    }
}
