import CoreLocation
import CTAKit
import SwiftUI

/// The full-screen map with the card list scrolling over it (redesign spec 2, 6).
struct MapScreen: View {
    @Environment(AppModel.self) private var model
    @Environment(ArrivalsStore.self) private var arrivals
    /// Our own follow state: only a real gesture clears it, and recenter sets it again.
    @State private var following = true

    var body: some View {
        GeometryReader { geo in
            ZStack {
                // The list covers the bottom at rest, so the camera centers in the part above it.
                DioramaMap(following: $following, focus: model.detailStop, busStops: busPins,
                           bottomInset: (geo.size.height * (1 - (model.detailStop == nil ? Theme.listRest : Theme.detailRest))).rounded()) { center in
                    model.center = center   // after a pan the list follows the center of the visible map
                }
                // Its own hosting controller, so the environment is passed in.
                PassThroughHost(openAbove: model.detailStop == nil ? model.listTop : model.detailTop) {
                    CardList().environment(model).environment(arrivals)
                }
            }
        }
        .ignoresSafeArea()
        // Detail swaps the controls for its hero: the big route number with × and ★ beside it.
        .overlay(alignment: .top) {
            if let hero = model.detailHero, model.detailStop != nil {
                DetailHeroView(hero: hero).transition(.opacity)
            } else if model.detailStop == nil {
                controls.transition(.opacity)
            }
        }
        .animation(Theme.snappy, value: model.detailStop == nil)
        .onAppear { model.location.start() }
        .onChange(of: fixKey, initial: true) { followFix() }
        .onChange(of: following) { followFix() }
    }

    /// Pins only for bus stops that are in the list (app spec 4.1): favorites, plus nearby unless location is off.
    private var busPins: [Stop] {
        let shown = model.slots.favorites + (model.location.denied ? [] : model.slots.nearby)
        var ids = Set(shown.filter { $0.kind == .bus }.map(\.fetchID))
        if let open = model.detailStop, open.kind == .bus { ids.insert(open.id) }   // the open stop, even from search
        return ids.sorted().compactMap { model.index?.stop(id: $0) }
    }

    /// The pet in its cream circle (with the gear: Settings), the "Near place" pill beside it, and recenter.
    private var controls: some View {
        HStack(alignment: .center, spacing: Theme.Space.s) {
            // The pill tucks under the avatar's right edge.
            ZStack(alignment: .leading) {
                NearLabel(center: model.center, leadingInset: Theme.Space.xl + Theme.Space.xs).padding(.leading, 40)
                PetAvatar { model.showSettings = true }
            }
            Spacer(minLength: 0)
            MapButton(symbol: following ? "location.fill" : "location", label: "Recenter", on: following) { following = true }
        }
        .padding(.horizontal, Theme.Space.l)
        .padding(.top, Theme.Space.xs)
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

/// A round solid map button on navy; its symbol turns headlight yellow while it's on.
struct MapButton: View {
    let symbol: String
    let label: String
    var on = false
    var fill = Theme.raised
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Image(systemName: symbol)
                .font(Theme.mapSymbol)
                .foregroundStyle(on ? Theme.headlight : .white)
                .frame(width: Theme.mapButton + Theme.Space.xs, height: Theme.mapButton + Theme.Space.xs)
                .surface(Circle(), fill: fill)
                .shadow(color: .black.opacity(0.35), radius: 8, y: 4)
        }
        .buttonStyle(PressableCard())
        .accessibilityLabel(label)
    }
}

/// The pixel pet in a cream circle with a gear badge: the app's face, and the way into Settings.
struct PetAvatar: View {
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            PixelImage(image: Glyphs.pet, points: 64)   // 2× the 32-pixel art
                .frame(width: 64, height: 64)
                .background(Theme.cream, in: Circle())
                .overlay { Circle().strokeBorder(Theme.navy, lineWidth: 3) }
                .overlay(alignment: .bottomTrailing) {
                    Image(systemName: "gearshape.fill")
                        .font(.system(size: 13, weight: .bold))
                        .foregroundStyle(.white)
                        .frame(width: 26, height: 26)
                        .background(Theme.navy, in: Circle())
                        .offset(x: 4, y: 4)
                }
                .shadow(color: .black.opacity(0.35), radius: 8, y: 4)
        }
        .buttonStyle(PressableCard())
        .accessibilityLabel("Settings")
    }
}

/// Over open stop detail: the chosen line's huge route number in its color, the stop under it, and the red ×
/// and ★ down the right (the detail screen's top, over the map).
struct DetailHeroView: View {
    let hero: DetailHero
    @Environment(AppModel.self) private var model

    var body: some View {
        let accent = Color(rgb: LineColors.onNavy(LineColors.accent(hero.line.route), minimum: 3))
        HStack(alignment: .top) {
            Text(hero.kind == .bus ? hero.line.route : RouteCard.shortName(hero.line))
                .font(.system(size: 96, weight: .heavy, design: .rounded)).tracking(-2)
                .lineLimit(1).minimumScaleFactor(0.4)
                .foregroundStyle(accent)
                .shadow(color: .black.opacity(0.6), radius: 10)
                .allowsHitTesting(false)   // the map under it keeps panning
            Spacer(minLength: Theme.Space.m)
            VStack(spacing: Theme.Space.m) {
                MapButton(symbol: "xmark", label: "Close", fill: Color(rgb: 0xE5484D)) { model.closeDetail += 1 }
                StarButton(favorite: Favorite(stopID: hero.stopID, line: hero.line))
            }
        }
        .padding(.horizontal, Theme.Space.l)
        .padding(.top, Theme.Space.xs)
    }
}

/// "Near *place*" on a capsule of material (app spec 4.1): reverse geocoded 1 s after the camera stops,
/// "Near you" on failure. The pet joins it in milestone 4.
struct NearLabel: View {
    let center: CLLocationCoordinate2D
    var leadingInset: CGFloat = Theme.Space.l
    @State private var place: String?

    var body: some View {
        Text(place.map { "Near \($0)" } ?? "Near you")
            .font(Theme.rounded(.headline, .heavy))
            .italic()
            .foregroundStyle(.white)
            .lineLimit(1)
            .padding(.leading, leadingInset)
            .padding(.trailing, Theme.Space.l)
            .frame(height: Theme.mapButton)
            .surface(Capsule())
            .shadow(color: .black.opacity(0.35), radius: 8, y: 4)
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
