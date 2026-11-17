import CTAKit
import MapKit
import SwiftUI

/// A place picked in search ("Wrigley Field", "3400 N Clark St"): the map flies there and the list shows its stops.
struct Place: Equatable {
    let name: String
    let lat: Double
    let lon: Double
    var coordinate: CLLocationCoordinate2D { CLLocationCoordinate2D(latitude: lat, longitude: lon) }
}

/// Places in search (Leo 2026-11-17): Apple's completer as you type, held to the CTA's area, and MKLocalSearch to
/// resolve the tapped one. No network or no match just means no places; stop search doesn't wait on it.
@Observable final class PlaceSearch: NSObject, @MainActor MKLocalSearchCompleterDelegate {
    /// The CTA's service area with a margin: Evanston to 130th, O'Hare to the lake.
    nonisolated static let chicago = MKCoordinateRegion(center: CLLocationCoordinate2D(latitude: 41.86, longitude: -87.72),
                                                        span: MKCoordinateSpan(latitudeDelta: 0.5, longitudeDelta: 0.5))

    private(set) var results: [MKLocalSearchCompletion] = []
    /// The query `results` answer (nil before the first answer), so "nothing matches" waits for places too.
    private(set) var answered: String?
    @ObservationIgnored private let completer = MKLocalSearchCompleter()

    override init() {
        super.init()
        completer.delegate = self
        completer.region = Self.chicago
        completer.regionPriority = .required
        completer.resultTypes = [.address, .pointOfInterest]
        // Apple's own transit stops ("Clark & Belmont Stop") repeat the app's stop rows.
        completer.pointOfInterestFilter = MKPointOfInterestFilter(excluding: [.publicTransport])
    }

    /// Asks for `query`'s places; an empty query clears them. The completer throttles and answers asynchronously.
    func update(_ query: String) {
        let q = query.trimmingCharacters(in: .whitespaces)
        if q.isEmpty { (results, answered) = ([], nil) }
        // Set even when empty: the same query typed again after a clear must ask again.
        if completer.queryFragment != q { completer.queryFragment = q }
    }

    // The completer calls back on the main thread, where it was made.
    func completerDidUpdateResults(_ completer: MKLocalSearchCompleter) {
        answer(completer.results, for: completer.queryFragment)
    }

    func completer(_ completer: MKLocalSearchCompleter, didFailWithError error: Error) {
        answer([], for: completer.queryFragment)
    }

    private func answer(_ r: [MKLocalSearchCompletion], for q: String) {
        guard !q.isEmpty else { return }
        var seen = Set<String>()   // Apple can list one place twice
        let unique = r.filter { seen.insert($0.key).inserted }
        withAnimation(Theme.snappy) { (results, answered) = (unique, q) }
    }

    /// Places shown: a short section after stop matches, more when they're the only results.
    nonisolated static func limit(stopHits: Int) -> Int { stopHits > 0 ? 3 : 6 }

    nonisolated static func contains(_ c: CLLocationCoordinate2D) -> Bool {
        abs(c.latitude - chicago.center.latitude) <= chicago.span.latitudeDelta / 2
            && abs(c.longitude - chicago.center.longitude) <= chicago.span.longitudeDelta / 2
    }

    /// The tapped completion's coordinate; nil offline, with no match, or outside the CTA's area.
    static func resolve(_ completion: MKLocalSearchCompletion) async -> Place? {
        let request = MKLocalSearch.Request(completion: completion)
        request.region = chicago
        request.regionPriority = .required
        guard let c = try? await MKLocalSearch(request: request).start().mapItems.first?.location.coordinate,
              contains(c) else { return nil }
        return Place(name: completion.title, lat: c.latitude, lon: c.longitude)
    }
}

/// A place in search results: its name, Apple's subtitle (the address or area), and a map pin.
struct PlaceRow: View {
    let completion: MKLocalSearchCompletion
    var loading = false

    var body: some View {
        HStack(spacing: Theme.Space.m) {
            Image(systemName: "mappin.circle.fill").font(.title2).foregroundStyle(Theme.cream)
                .accessibilityHidden(true)
            VStack(alignment: .leading, spacing: Theme.Space.xs) {
                Text(completion.title).font(Theme.rounded(.title3, .bold)).foregroundStyle(.white)
                let area = completion.subtitle.replacingOccurrences(of: ", United States", with: "")
                if !area.isEmpty {
                    Text(area).font(.subheadline.weight(.medium)).foregroundStyle(Theme.secondaryText)
                        .lineLimit(1)
                }
            }
            .multilineTextAlignment(.leading)
            Spacer(minLength: 0)
            if loading { ProgressView().tint(Theme.mist) } else {
                Image(systemName: "chevron.right").font(.caption.weight(.bold)).foregroundStyle(Theme.secondaryText)
                    .accessibilityHidden(true)
            }
        }
        .padding(.horizontal, Theme.cardMargin)
        .padding(.vertical, Theme.Space.l)
        .contentShape(.rect)
        .overlay(alignment: .bottom) { Theme.hairline.frame(height: 1).padding(.leading, Theme.cardMargin) }
        .accessibilityElement(children: .combine)
        .accessibilityHint("Shows stops near this place")
    }
}

/// The picked place on the map: its name on a cream pixel plate, with a stepped tail pointing at the spot.
struct PlacePin: View {
    let name: String

    var body: some View {
        VStack(spacing: 0) {
            Label(name, systemImage: "mappin")
                .font(Theme.rounded(.subheadline, .heavy))
                .foregroundStyle(Theme.navy)
                .lineLimit(1)
                .padding(.horizontal, Theme.Space.m)
                .padding(.vertical, Theme.Space.s)
                .plate(Theme.cream, rim: Theme.navy, rimWidth: 2, edge: 4, shape: .small)
            // Pixel steps, not a smooth triangle, like the rest of the art.
            ForEach([12, 8, 4], id: \.self) { Rectangle().fill(Theme.navy).frame(width: CGFloat($0), height: 3) }
        }
        .accessibilityElement(children: .combine)
    }
}

extension MKLocalSearchCompletion {
    /// Stable across answers (each is a new object), for row identity.
    var key: String { title + "|" + subtitle }
}
