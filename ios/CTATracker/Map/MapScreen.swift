import CoreLocation
import CTAKit
import MapKit
import SwiftUI

/// The full-screen map under the always-up route sheet (app spec 3, 4.1).
struct MapScreen: View {
    @Environment(AppModel.self) private var model
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    static let loopRegion = MapCameraPosition.region(MKCoordinateRegion(
        center: AppModel.loop, latitudinalMeters: 1600, longitudinalMeters: 1600))
    /// Follows the user (keeping their zoom) until they pan; the Loop when there's no location.
    @State private var camera: MapCameraPosition = .userLocation(fallback: MapScreen.loopRegion)
    /// Our own follow state: only a real drag clears it. MapKit marks the camera user-positioned on its own
    /// when the sheet's height changes the map's padding, so `camera.followsUserLocation` can't be trusted.
    @State private var following = true
    @State private var detent: PresentationDetent = .medium
    /// The camera and follow state from before stop detail opened, restored on back (app spec 3).
    @State private var beforeDetail: (camera: MapCameraPosition, following: Bool)?
    /// The last camera the map settled on: after a pan, the bound position holds no region to restore.
    @State private var lastCamera: MapCamera?

    var body: some View {
        GeometryReader { geo in
            // The sheet covers the bottom; up to half height the map keeps working behind it (app spec 3).
            let covered = min(max(0, geo.size.height - model.sheetTop), geo.size.height * 0.55)
            MapReader { proxy in
                map
                    .safeAreaPadding(.bottom, covered)
                    .onMapCameraChange(frequency: .onEnd) { context in
                        if model.detailStop == nil { lastCamera = context.camera }
                        // After a pan the list follows the center of the visible map. While following, it uses
                        // the user's fix instead, so resizing the sheet doesn't move the list (or refetch).
                        guard !following, model.detailStop == nil else { return }   // detail's camera isn't a pan
                        let visible = CGPoint(x: geo.size.width / 2, y: (geo.size.height - covered) / 2)
                        if let c = proxy.convert(visible, from: .local) { model.center = c }
                    }
                    .simultaneousGesture(DragGesture(minimumDistance: Theme.Space.m).onChanged { _ in following = false })
            }
        }
        .ignoresSafeArea()
        .overlay(alignment: .top) { controls }
        .onAppear { model.location.start() }
        .onChange(of: fixKey, initial: true) { followFix() }
        .onChange(of: model.detailStop?.id) { showDetailStop() }
        .sheet(isPresented: .constant(true)) {
            RouteSheet(detent: $detent)
                .presentationDetents([.height(140), .medium, .large], selection: $detent)
                .presentationBackgroundInteraction(.enabled(upThrough: .medium))
                .presentationContentInteraction(.scrolls)   // a pull at the top refreshes instead of resizing
                .presentationCornerRadius(Theme.cardRadius * 2)
                .interactiveDismissDisabled()
        }
    }

    private var map: some View {
        Map(position: $camera) {
            ForEach(busPins, id: \.id) { stop in
                Annotation(stop.name, coordinate: CLLocationCoordinate2D(latitude: stop.lat, longitude: stop.lon), anchor: .center) {
                    BusPin()
                }
                .annotationTitles(.hidden)
            }
            if let stop = model.detailStop {   // the open stop, which may not be in the list (opened from search)
                Marker(stop.name, systemImage: stop.kind == .bus ? "bus.fill" : "tram.fill",
                       coordinate: CLLocationCoordinate2D(latitude: stop.lat, longitude: stop.lon))
                    .tint(Color(rgb: LineColors.rgb(CardBuilder.slots(of: stop, meters: 0).first?.line.route ?? "")))
            }
            UserAnnotation()   // after the pins, so standing at a stop never hides the user's dot
        }
        .mapStyle(.standard(emphasis: .muted, pointsOfInterest: .excludingAll))
        .mapControls {}
    }

    /// Pins only for bus stops that are in the list (app spec 4.1): favorites, plus nearby unless location is off.
    private var busPins: [Stop] {
        let shown = model.slots.favorites + (model.location.denied ? [] : model.slots.nearby)
        return Set(shown.filter { $0.kind == .bus }.map(\.fetchID)).compactMap { model.index?.stop(id: $0) }
    }

    private var controls: some View {
        HStack(alignment: .top, spacing: Theme.Space.s) {
            MapButton(symbol: "gearshape.fill", label: "Settings") { model.showSettings = true }
            Spacer(minLength: 0)
            NearLabel(center: model.center)
            Spacer(minLength: 0)
            MapButton(symbol: following ? "location.fill" : "location", label: "Recenter") { recenter() }
        }
        .padding(.horizontal, Theme.Space.l)
        .padding(.top, Theme.Space.xs)
    }

    /// Stop detail centers the map on its stop; going back restores the previous camera (app spec 3, 5.1).
    /// Milestone 4 adds the line framing and live vehicles.
    private func showDetailStop() {
        if let stop = model.detailStop {
            if beforeDetail == nil {
                beforeDetail = (following ? camera : lastCamera.map { .camera($0) } ?? camera, following)
            }
            following = false
            let region = MKCoordinateRegion(center: CLLocationCoordinate2D(latitude: stop.lat, longitude: stop.lon),
                                            latitudinalMeters: 900, longitudinalMeters: 900)
            withAnimation(reduceMotion ? nil : Theme.smooth) { camera = .region(region) }
        } else if let before = beforeDetail {
            beforeDetail = nil
            following = before.following
            withAnimation(reduceMotion ? nil : Theme.smooth) { camera = before.camera }
            if following, let c = model.location.coordinate { model.center = c }
        }
    }

    /// Changes with each new fix (the location service filters them to every 30 m).
    private var fixKey: String {
        model.location.coordinate.map { "\($0.latitude),\($0.longitude)" } ?? ""
    }

    /// While following, the list centers on the user's fix, and the map is put back on the user (the first fix
    /// usually lands after the sheet has already nudged the camera).
    private func followFix() {
        guard following, let c = model.location.coordinate else { return }
        model.center = c
        if !camera.followsUserLocation {
            withAnimation(reduceMotion ? nil : Theme.smooth) { camera = .userLocation(fallback: Self.loopRegion) }
        }
    }

    /// Snaps the map and the list back to the user (app spec 4.1); jumps under Reduce Motion.
    private func recenter() {
        following = true
        withAnimation(reduceMotion ? nil : Theme.smooth) { camera = .userLocation(fallback: Self.loopRegion) }
        if let c = model.location.coordinate { model.center = c }
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

/// A listed bus stop on the map: a dot in the bus color with a white rim.
private struct BusPin: View {
    var body: some View {
        Circle()
            .fill(Color(rgb: LineColors.bus))
            .frame(width: Theme.Space.m, height: Theme.Space.m)
            .overlay(Circle().strokeBorder(.white, lineWidth: Theme.pinRim))
            .floatingShadow()
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
