import CoreLocation
import CTAKit
import SwiftUI

/// The full-screen map under the always-up route sheet (app spec 3, redesign spec 6).
struct MapScreen: View {
    @Environment(AppModel.self) private var model
    /// Our own follow state: only a real gesture clears it, and recenter sets it again.
    @State private var following = true
    @State private var detent: PresentationDetent = .medium

    var body: some View {
        GeometryReader { geo in
            // The sheet covers the bottom; up to half height the map keeps working behind it (app spec 3).
            let covered = min(max(0, geo.size.height - model.sheetTop), geo.size.height * 0.55)
            TimelineView(.everyMinute) { context in
                DioramaMap(following: $following, focus: model.detailStop, busStops: busPins, bottomInset: covered,
                           preset: preset(at: context.date)) { center in
                    model.center = center   // after a pan the list follows the center of the visible map
                }
            }
        }
        .ignoresSafeArea()
        .overlay(alignment: .top) { controls }
        .onAppear { model.location.start() }
        .onChange(of: fixKey, initial: true) { followFix() }
        .onChange(of: following) { followFix() }
        .sheet(isPresented: .constant(true)) {
            RouteSheet(detent: $detent)
                .presentationDetents([.height(140), .medium, .large], selection: $detent)
                .presentationBackgroundInteraction(.enabled(upThrough: .medium))
                .presentationContentInteraction(.scrolls)   // a pull at the top refreshes instead of resizing
                .presentationCornerRadius(Theme.cardRadius * 2)
                .interactiveDismissDisabled()
        }
    }

    /// Pins only for bus stops that are in the list (app spec 4.1): favorites, plus nearby unless location is off.
    private var busPins: [Stop] {
        let shown = model.slots.favorites + (model.location.denied ? [] : model.slots.nearby)
        var ids = Set(shown.filter { $0.kind == .bus }.map(\.fetchID))
        if let open = model.detailStop, open.kind == .bus { ids.insert(open.id) }   // the open stop, even from search
        return ids.sorted().compactMap { model.index?.stop(id: $0) }
    }

    private var controls: some View {
        HStack(alignment: .top, spacing: Theme.Space.s) {
            MapButton(symbol: "gearshape.fill", label: "Settings") { model.showSettings = true }
            Spacer(minLength: 0)
            NearLabel(center: model.center)
            Spacer(minLength: 0)
            MapButton(symbol: following ? "location.fill" : "location", label: "Recenter") { following = true }
        }
        .padding(.horizontal, Theme.Space.l)
        .padding(.top, Theme.Space.xs)
    }

    /// The map's light follows the sun at the list's center (redesign spec 6); polar days and nights read as day.
    private func preset(at now: Date) -> LightPreset {
        guard let sun = SunTimes.riseSet(on: now, lat: model.center.latitude, lon: model.center.longitude) else { return .day }
        return SunTimes.preset(at: now, rise: sun.rise, set: sun.set)
    }

    /// Changes with each new fix (the location service filters them to every 30 m).
    private var fixKey: String {
        model.location.coordinate.map { "\($0.latitude),\($0.longitude)" } ?? ""
    }

    /// While following (and no stop is open), the list centers on the user's fix.
    private func followFix() {
        guard following, model.detailStop == nil, let c = model.location.coordinate else { return }
        model.center = c
    }
}

/// A 44 pt circle on regular material, like Apple Maps' map controls (app spec 4.6).
struct MapButton: View {
    let symbol: String
    let label: String
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Image(systemName: symbol)
                .font(Theme.mapSymbol)
                .foregroundStyle(.primary)
                .frame(width: Theme.mapButton, height: Theme.mapButton)
                .materialBackground(Circle())
                .floatingShadow()
        }
        .buttonStyle(PressableCard())
        .accessibilityLabel(label)
    }
}

/// "Near *place*" on a capsule of material (app spec 4.1): reverse geocoded 1 s after the camera stops,
/// "Near you" on failure. The pet joins it in milestone 4.
struct NearLabel: View {
    let center: CLLocationCoordinate2D
    @State private var place: String?

    var body: some View {
        Text(place.map { "Near \($0)" } ?? "Near you")
            .font(.subheadline.weight(.semibold))
            .lineLimit(1)
            .padding(.horizontal, Theme.Space.l)
            .frame(height: Theme.mapButton)
            .materialBackground(Capsule())
            .floatingShadow()
            .task(id: "\(center.latitude),\(center.longitude)") {
                try? await Task.sleep(for: .seconds(1))
                guard !Task.isCancelled else { return }
                place = await Self.name(at: center)
            }
    }

    /// The neighborhood, else the street, else the city; nil on failure.
    static func name(at c: CLLocationCoordinate2D) async -> String? {
        let marks = try? await CLGeocoder().reverseGeocodeLocation(CLLocation(latitude: c.latitude, longitude: c.longitude))
        guard let m = marks?.first else { return nil }
        return m.subLocality ?? m.thoroughfare ?? m.locality
    }
}
