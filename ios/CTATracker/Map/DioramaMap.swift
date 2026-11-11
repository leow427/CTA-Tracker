import CTAKit
import MapboxMaps
import SwiftUI

/// The Mapbox diorama (redesign spec 6): muted Chicago in 3D or flat 2D, the L lines in their colors, pixel icons
/// for every station and the listed bus stops (tap one to open it), and the user's puck. It follows the puck
/// until a gesture clears `following`.
struct DioramaMap: View {
    @Binding var following: Bool
    /// The open stop: the camera flies to it, and clearing it restores the camera from before.
    let focus: Stop?
    /// Every station, and the listed bus stops, drawn as pixel icons.
    let stations: [Stop]
    let busStops: [Stop]
    /// Tilted with 3D buildings, or flat and top-down (redesign revision 3).
    let threeD: Bool
    /// The puck's fixes and heading, from the app's one location manager (`LocationService`).
    let puck: LocationDataModel
    /// How much of the map's bottom is covered, so the camera centers in the part left visible. Applied on each
    /// camera move: it changes only when detail opens or closes, and both move the camera.
    let bottomInset: CGFloat
    /// The center of the visible part, reported when the camera settles after the user moved it.
    let onCenter: (CLLocationCoordinate2D) -> Void
    /// A stop icon was tapped: its stop ID.
    let onStopTap: (String) -> Void

    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @Environment(\.displayScale) private var displayScale
    @State private var viewport = Viewport.camera(center: AppModel.loop, zoom: 14.5, bearing: 0, pitch: Self.pitch3D)
    @State private var beforeFocus: (viewport: Viewport, following: Bool)?

    static let pitch3D: CGFloat = 55
    private var pitch: CGFloat { threeD ? Self.pitch3D : 0 }
    static let zoom: CGFloat = 15.5

    var body: some View {
        MapReader { proxy in
            Map(viewport: $viewport) {
                Puck2D(bearing: .heading)
                GeoJSONSource(id: "l").data(.featureCollection(Self.lines))
                LineLayer(id: "l-case", source: "l")
                    .lineColor(.white)
                    .lineWidth(Self.width(plus: 2))
                    .lineCap(.round).lineJoin(.round)
                    .lineEmissiveStrength(1)
                    .lineOcclusionOpacity(0.35)
                    .slot(.middle)
                LineLayer(id: "l", source: "l")
                    .lineColor(Self.lineColors)
                    .lineWidth(Self.width(plus: 0))
                    .lineCap(.round).lineJoin(.round)
                    .lineEmissiveStrength(1)
                    .lineOcclusionOpacity(0.35)
                    .slot(.middle)
                let art = stationArt()
                ForEvery(art.icons + StopIcons.images(busStops, displayScale: displayScale), id: \.id) { $0.image }
                GeoJSONSource(id: "stations").data(.featureCollection(art.features))
                GeoJSONSource(id: "buses").data(.featureCollection(features(busStops)))
                // Crowded stations (the Loop zoomed out) drop instead of piling up, the open one last.
                stopLayer("stations").iconAllowOverlap(false).symbolSortKey(Exp(.get) { "rank" })
                // The few listed bus stops always show, even beside a station, without pushing stations out.
                stopLayer("buses").iconAllowOverlap(true).iconIgnorePlacement(true)
                TapInteraction(.layer("stations")) { feature, _ in tapped(feature) }
                TapInteraction(.layer("buses")) { feature, _ in tapped(feature) }
            }
            .mapStyle(threeD ? Self.navy3D : Self.navy2D)
            .locationDataModel(puck)
            .frameRate(range: 30...60, preferred: 60)   // at most 60 fps: 120 on ProMotion heated the phone
            .ornamentOptions(OrnamentOptions(
                scaleBar: ScaleBarViewOptions(visibility: .hidden),
                compass: CompassViewOptions(visibility: .hidden),
                logo: LogoViewOptions(position: .bottomLeading, margins: CGPoint(x: Theme.Space.l, y: ornamentY)),
                attributionButton: AttributionButtonOptions(position: .bottomTrailing,
                                                            margins: CGPoint(x: Theme.Space.l, y: ornamentY))))
            .gestureHandlers(MapGestureHandlers(
                // A tap (on a stop, say) doesn't move the camera, so it keeps following; but Mapbox cancels camera
                // animations on every tap, which idles a follow still flying in (on the next main-queue turn), so
                // follow again after that.
                onBegin: { if $0 != .singleTap { following = false } },
                onEnd: { gesture, _ in
                    guard gesture == .singleTap else { return }
                    DispatchQueue.main.async { if following { move(to: follow) } }
                }))
            .onMapIdle { _ in
                guard !following, focus == nil, let c = proxy.map?.cameraState.center else { return }
                onCenter(c)
            }
            // Following without a fix yet shows no camera at all, so then the pitch is set directly (at load and on
            // each switch); with a fix, the follow flies to the new pitch.
            .onMapLoaded { _ in proxy.map?.setCamera(to: CameraOptions(pitch: pitch)) }
            .onChange(of: threeD) {
                // Tilt or flatten in place: following re-follows at the new pitch.
                guard !following, let camera = proxy.map?.cameraState else {
                    if proxy.location?.latestLocation == nil { proxy.map?.setCamera(to: CameraOptions(pitch: pitch)) }
                    return move(to: follow)
                }
                move(to: .camera(center: camera.center, zoom: camera.zoom, bearing: camera.bearing, pitch: pitch))
            }
        }
        .ignoresSafeArea()
        .onChange(of: following, initial: true) { if following { move(to: follow) } }
        .onChange(of: focus?.id) { showFocus() }
    }

    /// The logo and info button sit just above the covered part; over stop detail, also above its header.
    private var ornamentY: CGFloat { bottomInset + (focus == nil ? Theme.Space.s : Theme.Space.xxl * 4) }

    private var follow: Viewport { .followPuck(zoom: Self.zoom, bearing: .constant(0), pitch: pitch) }

    private func move(to target: Viewport) {
        let padded = target.padding(.bottom, bottomInset)
        if reduceMotion { viewport = padded } else { withViewportAnimation(.default(maxDuration: 1.2)) { viewport = padded } }
    }

    private func showFocus() {
        if let stop = focus {
            if beforeFocus == nil { beforeFocus = (viewport, following) }
            following = false
            move(to: .camera(center: CLLocationCoordinate2D(latitude: stop.lat, longitude: stop.lon), zoom: Self.zoom,
                             bearing: 0, pitch: pitch))
        } else if let before = beforeFocus {
            beforeFocus = nil
            following = before.following
            if !following { move(to: before.viewport) }   // following re-follows through its own onChange
        }
    }

    /// Stops as points with their id, icon, and rank (the open stop first in collisions).
    private func features(_ stops: [Stop]) -> FeatureCollection {
        FeatureCollection(features: stops.map { stop in
            var f = Feature(geometry: .point(Point(CLLocationCoordinate2D(latitude: stop.lat, longitude: stop.lon))))
            f.properties = ["id": .string(stop.id), "icon": .string(StopIcons.id(stop)),
                            "rank": .number(stop.id == focus?.id ? 0 : 1)]
            return f
        })
    }

    /// The stations' icons and features, made again only when the stations, the open stop, or the screen scale change:
    /// the map's body runs on every change to the list's bus stops, and the ~150 stations rarely change.
    private func stationArt() -> (icons: [(id: String, image: StyleImage)], features: FeatureCollection) {
        if let c = Self.stationCache, c.stations == stations, c.focus == focus?.id, c.scale == displayScale { return c.art }
        let art = (StopIcons.images(stations, displayScale: displayScale), features(stations))
        Self.stationCache = (stations, focus?.id, displayScale, art)
        return art
    }

    private static var stationCache: (stations: [Stop], focus: String?, scale: CGFloat,
                                      art: (icons: [(id: String, image: StyleImage)], features: FeatureCollection))?

    private func tapped(_ feature: FeaturesetFeature) -> Bool {
        if case .string(let id)? = feature.properties["id"] { onStopTap(id) }
        return true
    }

    /// A pixel-icon layer: flat to the screen, never tilted or rotated, lit like the lines.
    private func stopLayer(_ id: String) -> SymbolLayer {
        SymbolLayer(id: id, source: id)
            .iconImage(Exp(.get) { "icon" })
            .iconPitchAlignment(.viewport)
            .iconRotationAlignment(.viewport)
            .iconEmissiveStrength(1)
            .slot(.top)
    }

    // MARK: - Static style pieces

    /// Made once: the map's body runs often, and the style needn't be rebuilt each time.
    static let navy3D = navyStyle(threeD: true)
    static let navy2D = navyStyle(threeD: false)

    /// Dark navy Chicago (Transit-style): Standard's night light over navy land, roads, water, and buildings,
    /// still in 3D, so the L lines and the list's colors carry the brightness.
    static func navyStyle(threeD: Bool) -> MapStyle { .standard(
        lightPreset: .day,   // daylight shading over the navy palette keeps roads and 3D buildings visible
        showPointOfInterestLabels: false,
        showTransitLabels: false,
        show3dObjects: threeD,
        colorBuildings: StyleColor(UIColor(Color(rgb: 0x1E3460))),
        colorCommercial: StyleColor(UIColor(Color(rgb: 0x132650))),
        colorEducation: StyleColor(UIColor(Color(rgb: 0x132650))),
        colorGreenspace: StyleColor(UIColor(Color(rgb: 0x123A44))),
        colorLand: StyleColor(UIColor(Color(rgb: land))),
        colorMedical: StyleColor(UIColor(Color(rgb: 0x132650))),
        colorMotorways: StyleColor(UIColor(Color(rgb: 0x2D4A86))),
        colorRoadLabels: StyleColor(UIColor(Color(rgb: 0x8FA6D6))),
        colorRoads: StyleColor(UIColor(Color(rgb: 0x2A4377))),
        colorTrunks: StyleColor(UIColor(Color(rgb: 0x2D4A86))),
        colorWater: StyleColor(UIColor(Color(rgb: 0x0A1730)))) }

    /// The map's land color, which the main page's wash also uses.
    static let land: UInt32 = 0x10224A

    /// Every train shape as one line feature with its `route`.
    static let lines: FeatureCollection = {
        let shapes = Bundle.main.url(forResource: "train_shapes", withExtension: "json")
            .flatMap { try? Data(contentsOf: $0) }.flatMap { try? TrainShapes.decode($0) } ?? []
        return FeatureCollection(features: shapes.map { shape in
            var f = Feature(geometry: .lineString(LineString(shape.points.map {
                CLLocationCoordinate2D(latitude: $0[0], longitude: $0[1])
            })))
            f.properties = ["route": .string(shape.route)]
            return f
        })
    }()

    /// The line's CTA color by its `route` property.
    static let lineColors: Exp = Exp(.match) {
        Exp(.get) { "route" }
        for line in LineColors.trains {
            line.route
            UIColor(Color(rgb: line.rgb))
        }
        UIColor(Color(rgb: LineColors.bus))
    }

    /// About 3 pt at z12 to 7 pt at z16, plus `extra` for the casing.
    static func width(plus extra: Double) -> Exp {
        Exp(.interpolate) {
            Exp(.linear)
            Exp(.zoom)
            12
            3 + extra
            16
            7 + extra
        }
    }
}
