import CTAKit
import SwiftUI

/// Picks one dock section's stop (ecosystem spec 5.3): the app's search (`StopSearch`), then a card, a line and
/// direction at a bus stop or a train platform. Choosing one sends all 3 sections and closes.
struct DockStopPicker: View {
    let dock: String
    let section: Int
    @Environment(AppModel.self) private var model
    @Environment(\.dismiss) private var dismiss
    @State private var query = ""
    @State private var found: StopSearch.Results?
    @State private var sending = false
    @State private var failed = false

    var body: some View {
        NavigationStack {
            List {
                if let found {
                    if !found.favorites.isEmpty {
                        Section("Favorites") {
                            ForEach(found.favorites) { slot in
                                if let stop = model.index?.stop(id: slot.fetchID) { card(slot, name: stop.name, showName: true) }
                            }
                        }
                    }
                    if !found.stops.isEmpty {
                        Section("Stops") {
                            ForEach(found.stops, id: \.stop.id) { hit in
                                DisclosureGroup(hit.stop.name) {
                                    ForEach(CardBuilder.slots(of: hit.stop, meters: 0)) { card($0, name: hit.stop.name, showName: false) }
                                }
                            }
                        }
                    }
                }
            }
            .searchable(text: $query, placement: .navigationBarDrawer(displayMode: .always), prompt: "Search stops and routes")
            .task(id: query) { await search() }
            .navigationTitle("Section \(section + 1)")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar { ToolbarItem(placement: .cancellationAction) { Button("Cancel") { dismiss() } } }
            .alert("Couldn't reach the dock", isPresented: $failed) { Button("OK") {} }
            .disabled(sending)
        }
    }

    /// One card to pick: a train's platform with its direction, or a bus route and direction.
    private func card(_ slot: Slot, name: String, showName: Bool) -> some View {
        Button {
            sending = true
            Task {
                let stop = ConfigStop(dock: slot.line, kind: slot.kind, stopID: slot.stopID, name: name)
                if await model.sendToDock(stop, section: section, dock: dock) { dismiss() } else { failed = true }
                sending = false
            }
        } label: {
            VStack(alignment: .leading, spacing: 2) {
                if showName { Text(name) }
                Text(slot.kind == .bus ? "\(slot.line.route) · \(slot.line.dir)" : "\(LineColors.train(slot.line.route)?.name ?? slot.line.route) toward \(slot.line.dir)")
                    .font(showName ? .subheadline : .body)
                    .foregroundStyle(showName ? Theme.secondaryText : .primary)
            }
        }
    }

    /// The list's search, debounced and off the main thread, as in `CardList`.
    private func search() async {
        guard let index = await model.search else { return }
        if found != nil { try? await Task.sleep(for: .milliseconds(120)) }
        guard !Task.isCancelled else { return }
        let (q, center, favorites) = (query, model.center, model.allFavoriteSlots)
        let r = await Task.detached(priority: .userInitiated) {
            StopSearch.results(q, center: center, favorites: favorites, index: index)
        }.value
        if !Task.isCancelled { found = r }
    }
}
