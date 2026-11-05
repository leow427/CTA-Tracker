import CTAKit
import MapboxMaps
import SwiftUI

/// The Mapbox diorama (redesign spec 6): muted 3D Chicago, the L lines in their colors, listed bus stops,
/// and the user's puck. It follows the puck until a gesture clears `following`.
struct DioramaMap: View {
    @Binding var following: Bool
    /// The open stop: the camera flies to it, and clearing it restores the camera from before.
    let focus: Stop?
    let busStops: [Stop]
    /// How much of the map's bottom is covered, so the camera centers in the part left visible. Applied on each
    /// camera move: it changes only when detail opens or closes, and both move the camera.
    let bottomInset: CGFloat
    /// The center of the visible part, reported when the camera settles after the user moved it.
    let onCenter: (CLLocationCoordinate2D) -> Void

    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @State private var viewport = Viewport.camera(center: AppModel.loop, zoom: 14.5, bearing: 0, pitch: Self.pitch)
    @State private var beforeFocus: (viewport: Viewport, following: Bool)?

    static let pitch: CGFloat = 55
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
                GeoJSONSource(id: "bus").data(.featureCollection(busFeatures))
                CircleLayer(id: "bus", source: "bus")
                    .circleRadius(6)
                    .circleColor(UIColor(Color(rgb: LineColors.busAccent)))   // the bus blue, as in the rows
                    .circleStrokeColor(.white)
                    .circleStrokeWidth(Theme.pinRim)
                    .circleEmissiveStrength(1)
                    .slot(.top)
            }
            .mapStyle(Self.navyStyle)
            .ornamentOptions(OrnamentOptions(
                scaleBar: ScaleBarViewOptions(visibility: .hidden),
                compass: CompassViewOptions(visibility: .hidden),
                logo: LogoViewOptions(position: .bottomLeading, margins: CGPoint(x: Theme.Space.l, y: bottomInset + Theme.Space.s)),
                attributionButton: AttributionButtonOptions(position: .bottomTrailing,
                                                            margins: CGPoint(x: Theme.Space.l, y: bottomInset + Theme.Space.s))))
            .gestureHandlers(MapGestureHandlers(onBegin: { _ in following = false }))
            .onMapIdle { _ in
                guard !following, focus == nil, let c = proxy.map?.cameraState.center else { return }
                onCenter(c)
            }
        }
        .ignoresSafeArea()
        .onChange(of: following, initial: true) { if following { move(to: .followPuck(zoom: Self.zoom, bearing: .constant(0), pitch: Self.pitch)) } }
        .onChange(of: focus?.id) { showFocus() }
    }

    private func move(to target: Viewport) {
        let padded = target.padding(.bottom, bottomInset)
        if reduceMotion { viewport = padded } else { withViewportAnimation(.default(maxDuration: 1.2)) { viewport = padded } }
    }

    private func showFocus() {
        if let stop = focus {
            if beforeFocus == nil { beforeFocus = (viewport, following) }
            following = false
            move(to: .camera(center: CLLocationCoordinate2D(latitude: stop.lat, longitude: stop.lon), zoom: Self.zoom,
                             bearing: 0, pitch: Self.pitch))
        } else if let before = beforeFocus {
            beforeFocus = nil
            following = before.following
            if !following { move(to: before.viewport) }   // following re-follows through its own onChange
        }
    }

    private var busFeatures: FeatureCollection {
        FeatureCollection(features: busStops.map {
            Feature(geometry: .point(Point(CLLocationCoordinate2D(latitude: $0.lat, longitude: $0.lon))))
        })
    }

    // MARK: - Static style pieces

    /// Dark navy Chicago (Transit-style): Standard's night light over navy land, roads, water, and buildings,
    /// still in 3D, so the L lines and the list's colors carry the brightness.
    static let navyStyle = MapStyle.standard(
        lightPreset: .day,   // daylight shading over the navy palette keeps roads and 3D buildings visible
        showPointOfInterestLabels: false,
        showTransitLabels: false,
        show3dObjects: true,
        colorBuildings: StyleColor(UIColor(Color(rgb: 0x1E3460))),
        colorCommercial: StyleColor(UIColor(Color(rgb: 0x132650))),
        colorEducation: StyleColor(UIColor(Color(rgb: 0x132650))),
        colorGreenspace: StyleColor(UIColor(Color(rgb: 0x123A44))),
        colorLand: StyleColor(UIColor(Color(rgb: 0x10224A))),
        colorMedical: StyleColor(UIColor(Color(rgb: 0x132650))),
        colorMotorways: StyleColor(UIColor(Color(rgb: 0x2D4A86))),
        colorRoadLabels: StyleColor(UIColor(Color(rgb: 0x8FA6D6))),
        colorRoads: StyleColor(UIColor(Color(rgb: 0x2A4377))),
        colorTrunks: StyleColor(UIColor(Color(rgb: 0x2D4A86))),
        colorWater: StyleColor(UIColor(Color(rgb: 0x0A1730))))

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
