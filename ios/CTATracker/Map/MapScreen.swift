import CoreLocation
import CTAKit
import SwiftUI

/// The full-screen map with the card list scrolling over it (redesign spec 2, 6).
struct MapScreen: View {
    @Environment(AppModel.self) private var model
    @Environment(ArrivalsStore.self) private var arrivals
    /// Our own follow state: only a real gesture clears it, and recenter sets it again.
    @State private var following = true
    /// Tilted 3D or flat 2D, kept between launches (redesign revision 3).
    @AppStorage("map3D") private var threeD = true

    var body: some View {
        GeometryReader { geo in
            ZStack {
                // The list covers the bottom at rest, so the camera centers in the part above it.
                DioramaMap(following: $following, focus: model.detailStop, stops: model.stations + busPins, threeD: threeD,
                           bottomInset: (geo.size.height * (1 - (model.detailStop == nil ? Theme.listRest : Theme.detailRest))).rounded(),
                           onCenter: { model.center = $0 },   // after a pan the list follows the center of the visible map
                           onStopTap: { model.openStop = $0 })
                wash.allowsHitTesting(false)
                // Its own hosting controller, so the environment is passed in.
                PassThroughHost(openAbove: { [model] in model.detailStop == nil ? model.listTop : model.detailTop }) {
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

    /// Under the list, a subtle wash in a darker shade of the map's navy: clear in the top fifth, deepening to mostly
    /// opaque at the bottom (Leo, 2026-11-10). Under stop detail, the line's dark shade instead: clear at the top, solid over the
    /// bottom 20% (revision 3).
    @ViewBuilder private var wash: some View {
        if let hero = model.detailHero, model.detailStop != nil {
            let shade = Color.card(hero.line.route)
            LinearGradient(stops: [.init(color: shade.opacity(0), location: 0), .init(color: shade, location: 0.8)],
                           startPoint: .top, endPoint: .bottom)
                .transition(.opacity)
                .animation(Theme.smooth, value: hero.line.route)
        } else {
            // The map's navy taken darker: in its own land color the wash vanished into the map.
            let shade = Color(rgb: Self.listWash)
            LinearGradient(stops: [.init(color: shade.opacity(0), location: 0.2),
                                   .init(color: shade.opacity(0.6), location: 0.75),
                                   .init(color: shade.opacity(0.85), location: 1)],
                           startPoint: .top, endPoint: .bottom)
                .transition(.opacity)
        }
    }

    /// Pins for the listed bus stops nearest the map's center (favorites, plus nearby unless location is off).
    private var busPins: [Stop] {
        let shown = model.slots.favorites + (model.location.denied ? [] : model.slots.nearby)
        // Only the few nearest the map's center (Leo, 2026-11-10): the list re-centers on pans, so they follow the view.
        var ids: [String] = []
        for slot in shown.filter({ $0.kind == .bus }).sorted(by: { $0.meters < $1.meters })
            where !ids.contains(slot.fetchID) && ids.count < Self.busPinCount { ids.append(slot.fetchID) }
        if let open = model.detailStop, open.kind == .bus, !ids.contains(open.id) { ids.append(open.id) }   // even from search
        return ids.compactMap { model.index?.stop(id: $0) }
    }

    static let busPinCount = 5
    /// The main page's wash: the map's land navy, darker.
    static let listWash: UInt32 = 0x050C1F

    /// The pet at the top left (its gear: Settings), saying where the map is in a speech bubble, and recenter.
    private var controls: some View {
        HStack(alignment: .center, spacing: Theme.Space.m) {
            PetAvatar(mood: petMood(now: .now)) { model.showSettings = true }
            NearLabel(center: model.center)
            Spacer(minLength: 0)
            VStack(spacing: Theme.Space.m) {
                MapButton(symbol: following ? "location.fill" : "location", label: "Recenter", on: following) { following = true }
                // Shows the mode it switches to, like Maps.
                MapButton(symbol: threeD ? "view.2d" : "view.3d", label: threeD ? "Flat map" : "3D map") { threeD.toggle() }
                    .sensoryFeedback(.selection, trigger: threeD)
            }
        }
        .padding(.horizontal, Theme.Space.l)
        .padding(.top, Theme.Space.xs)
    }

    /// The pet's mood by the devices' rules over the listed cards (app spec 4.5): only the 3 closest stops can make it
    /// Hyped, and no location yet means Sleepy.
    /// shortcut: one record per card, where the devices count each stop once; fine until the dock relays these.
    private func petMood(now: Date) -> Mood {
        let slots = model.slots.favorites + (model.location.denied ? [] : model.slots.nearby)
        var nearest: [String] = []
        for slot in slots.sorted(by: { $0.meters < $1.meters }) where !nearest.contains(slot.fetchID) {
            nearest.append(slot.fetchID)
        }
        let close = Set(nearest.prefix(Config.hypedClosestStops))
        let stops = slots.map { slot in
            (record: StopRecord.make(id: slot.fetchID, kind: slot.kind, name: "", from: arrivals.cardPredictions(slot, now: now),
                                     alert: arrivals.hasAlert(slot), fetched: arrivals.fetched[slot.fetchID] ?? .distantPast),
             canHype: close.contains(slot.fetchID))
        }
        return MoodRules.deviceMood(stops, now: now, night: Night.now(now), hasLocation: model.location.coordinate != nil)
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
                .plate(fill, edge: 4, shape: .small)
        }
        .buttonStyle(PressableCard())
        .accessibilityLabel(label)
    }
}

/// The train pet on a cream tile at the top left, the app's face and the way into Settings (the gear). Its face
/// shows its mood, with a little extra (a heart, Zz) floating at its corner. It bobs a pixel twice a second, like
/// a game character waiting, and holds still under Reduce Motion.
struct PetAvatar: View {
    let mood: Mood
    let action: () -> Void
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    var body: some View {
        Button(action: action) {
            TimelineView(.periodic(from: .now, by: 0.5)) { context in
                let up = !reduceMotion && Int(context.date.timeIntervalSinceReferenceDate * 2) % 2 == 0
                PixelImage(image: Glyphs.pet(mood), points: 96)   // 3× the 32-pixel art
                    .offset(y: up ? -3 : 0)   // one art pixel
                    .frame(width: Theme.avatar, height: Theme.avatar)
                    .plate(Theme.cream, rim: Theme.navy, rimWidth: 3)
                    .overlay(alignment: .topTrailing) {
                        if let extra = Glyphs.petExtra(mood) {
                            PixelImage(image: extra, points: 21)   // 3×, the pet's scale
                                .offset(x: 16, y: up ? -12 : -9)   // bobs with the pet (Leo, 2026-11-07)
                        }
                    }
            }
            .overlay(alignment: .bottomTrailing) {
                Image(systemName: "gearshape.fill")
                    .font(.system(size: 14, weight: .bold))
                    .foregroundStyle(Theme.navy)
                    .frame(width: 30, height: 30)
                    .plate(Theme.headlight, rim: Theme.navy, rimWidth: 2, edge: 3, shape: .small)
                    .offset(x: 6, y: 6)
            }
        }
        .buttonStyle(PressableCard())
        .accessibilityLabel("Settings")
        .accessibilityValue("Your pet is \(mood.rawValue)")
    }
}

/// A speech bubble's pixel tail, pointing left at the pet.
nonisolated private struct BubbleTail: Shape {
    func path(in r: CGRect) -> Path {
        let (x, y) = (r.width / 2, r.height / 3)
        var p = Path()
        p.addLines([CGPoint(x: x, y: 0), CGPoint(x: r.width, y: 0), CGPoint(x: r.width, y: r.height),
                    CGPoint(x: x, y: r.height), CGPoint(x: x, y: 2 * y), CGPoint(x: 0, y: 2 * y),
                    CGPoint(x: 0, y: y), CGPoint(x: x, y: y)])
        p.closeSubpath()
        return p
    }
}

/// Over open stop detail: a bus's huge route number in its color, and the red × and ★ down the right
/// (the detail screen's top, over the map).
struct DetailHeroView: View {
    let hero: DetailHero
    @Environment(AppModel.self) private var model

    var body: some View {
        let name = Text(hero.line.route)
            .font(.system(size: 96, weight: .heavy, design: .rounded)).tracking(-2)
        HStack(alignment: .top) {
            // A bus's number, with a hard pixel drop shadow in the card's dark shade, like a game title. Trains show
            // none: the wash's color names the line (revision 3).
            if hero.kind == .bus {
                ZStack(alignment: .topLeading) {
                    name.foregroundStyle(Color.card(hero.line.route)).offset(x: 6, y: 6)
                    name.foregroundStyle(Color.cardText(hero.line.route))
                }
                .lineLimit(1).minimumScaleFactor(0.4)
                .allowsHitTesting(false)   // the map under it keeps panning
            }
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

/// What the pet says: "Near *place*" in a cream pixel speech bubble (app spec 4.1), reverse geocoded 1 s after the
/// camera stops, "Near you" on failure.
struct NearLabel: View {
    let center: CLLocationCoordinate2D
    @State private var place: String?

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            Text("Near").font(Theme.rounded(.caption, .heavy)).textCase(.uppercase).tracking(1).opacity(0.6)
            Text(place ?? "you").font(Theme.rounded(.headline, .heavy))
                .lineLimit(1).minimumScaleFactor(0.7)
        }
        .foregroundStyle(Theme.navy)
        .padding(.horizontal, Theme.Space.m)
        .padding(.vertical, Theme.Space.s)
        .plate(Theme.cream, rim: Theme.navy, rimWidth: 2, edge: 4, shape: .small)
        .overlay(alignment: .leading) {
            BubbleTail().fill(Theme.cream).frame(width: 8, height: 12).offset(x: -6)
        }
        .accessibilityElement(children: .combine)
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
