import CTAKit
import Foundation
import Observation
import os
import UIKit

/// The relay (plan, "Relay"; Task AB3): while the app is in the foreground and a wearable is on Bluetooth, every 30 s
/// it sends `/relay` with the phone's time, then `/stop` for each of the wearable's stops (its last `nearby` plus the
/// pin) fetched since its last send, and each at least every 60 s. Bluetooth only: on Wi-Fi the wearable polls CTA.
@Observable final class WearableRelay {
    nonisolated static let resendSec: TimeInterval = 60

    private(set) var running = false
    @ObservationIgnored private let link: DeviceLink
    @ObservationIgnored private let arrivals: ArrivalsStore
    @ObservationIgnored private let index: StopIndex?
    @ObservationIgnored private var loop: Task<Void, Never>?
    @ObservationIgnored private var lastSent: [String: (at: Date, fetched: Date)] = [:]   // "device|stop"
    @ObservationIgnored private var lifecycle: [NSObjectProtocol] = []
    private let log = Logger(subsystem: "io.github.leow427.CTATracker", category: "relay")

    init(link: DeviceLink, arrivals: ArrivalsStore, index: StopIndex?) {
        self.link = link
        self.arrivals = arrivals
        self.index = index
        link.onSent = { [weak self] in if self?.running == true { self?.setActive(true) } }   // a fresh tick now
        // Foreground only (plan, "iOS: what really works in the background").
        lifecycle = [(UIApplication.didBecomeActiveNotification, true), (UIApplication.willResignActiveNotification, false)]
            .map { name, active in
                NotificationCenter.default.addObserver(forName: name, object: nil, queue: .main) { [weak self] _ in
                    MainActor.assumeIsolated { self?.setActive(active) }
                }
            }
    }

    func setActive(_ active: Bool) {
        loop?.cancel()
        loop = nil
        running = active
        guard active else { arrivals.extra = []; return }
        loop = Task { [weak self] in
            while !Task.isCancelled {
                await self?.tick()
                try? await Task.sleep(for: .seconds(Config.relayTickSec))
            }
        }
    }

    // MARK: - Pure parts (tested)

    nonisolated static func tick(now: Date) -> RelayTick {
        // shortcut: no weather, since the app has no weather fetch yet; add wmo and temp when it does.
        RelayTick(now: Int(now.timeIntervalSince1970), wmo: nil, temp: nil)
    }

    /// The wearable's stops: its nearby stations and bus stops, then the pin's stop, each once.
    nonisolated static func stops(nearby: NearbyMessage?, pin: ConfigStop?, index: StopIndex) -> [Stop] {
        let ids = (nearby?.trains ?? []) + (nearby?.buses ?? [])
        var out: [Stop] = []
        for stop in ids.compactMap(index.stop(id:)) + [pin.flatMap { index.stop(id: $0.id) ?? index.station(ofPlatform: $0.id) }].compactMap({ $0 })
        where !out.contains(where: { $0.id == stop.id }) {
            out.append(stop)
        }
        return out
    }

    /// One record per line and direction with arrivals, keyed as the wearable keys its own cards (station or bus
    /// stop ID, route, the platform's direction label), soonest first.
    nonisolated static func records(for stop: Stop, predictions: [Prediction], fetched: Date,
                                    alert: (String) -> Bool) -> [StopRecord] {
        CardBuilder.slots(of: stop, meters: 0)
            .map { ($0, $0.predictions(from: predictions, now: fetched)) }
            .filter { !$0.1.isEmpty }
            .sorted { $0.1[0].at < $1.1[0].at }
            .map { slot, preds in
                // Train arrivals carry no direction of their own: the card's label goes on them, so `make` keys it.
                let labeled = preds.map {
                    Prediction(stopID: $0.stopID, route: $0.route, dir: slot.line.dir, dest: $0.dest, at: $0.at,
                               live: $0.live, delayed: $0.delayed, vehicle: $0.vehicle)
                }
                return StopRecord.make(id: stop.id, kind: stop.kind, name: stop.name, from: labeled,
                                       alert: alert(slot.line.route), fetched: fetched)
            }
    }

    /// Only wearables the app has told their stops: a `/relay` alone would stop the wearable's own polling with
    /// nothing to show (after a relaunch, until `nearby` goes out again).
    nonisolated static func targets(_ wearables: [String], sentNearby: [String: NearbyMessage]) -> [String] {
        wearables.filter { sentNearby[$0] != nil }
    }

    nonisolated static func due(fetched: Date?, last: (at: Date, fetched: Date)?, now: Date) -> Bool {
        guard let fetched else { return false }
        guard let last else { return true }
        return fetched > last.fetched || now.timeIntervalSince(last.at) >= resendSec
    }

    // MARK: - Sending

    private func tick() async {
        guard let index else { return }
        let connected = link.devices.filter { $0.kind == .handheld && link.ble.connected[$0.id] != nil }.map(\.id)
        let wearables = Self.targets(connected, sentNearby: link.sentNearby)
        let stopsByDevice = wearables.map { ($0, Self.stops(nearby: link.sentNearby[$0], pin: link.sentPin[$0], index: index)) }
        let all = stopsByDevice.flatMap(\.1)
        arrivals.extra = all.flatMap { CardBuilder.slots(of: $0, meters: 0) }
        guard !wearables.isEmpty else { return }

        // Ride the list's fetches; fetch only what they haven't covered in the last tick.
        let now = Date()
        let stale = Set(all.map(\.id)).filter { arrivals.fetched[$0].map { now.timeIntervalSince($0) >= Config.relayTickSec } ?? true }
        if !stale.isEmpty { await arrivals.refresh(only: stale) }

        // Cancelled (the app went to the background, or a new stop set restarted the loop): send nothing more.
        for (id, stops) in stopsByDevice {
            guard !Task.isCancelled else { return }
            await send("/relay", BLEMessage.encode(Self.tick(now: Date()), limit: BLEFrames.maxMessage), to: id)
            for stop in stops {
                guard !Task.isCancelled else { return }
                let key = "\(id)|\(stop.id)", fetched = arrivals.fetched[stop.id], now = Date()
                guard let fetched, Self.due(fetched: fetched, last: lastSent[key], now: now) else { continue }
                let records = Self.records(for: stop, predictions: arrivals.predictions[stop.id] ?? [], fetched: fetched) {
                    !(arrivals.alerts[$0] ?? []).isEmpty
                }
                let body = RelayStop(id: stop.id, type: stop.kind == .bus ? .bus : .train, records: records)
                if await send("/stop", BLEMessage.encode(body, limit: BLEFrames.maxMessage), to: id) {
                    lastSent[key] = (now, fetched)
                }
            }
        }
    }

    /// Over the bond only, never HTTP. A body too big to frame is skipped; the next tick tries again.
    @discardableResult private func send(_ path: String, _ body: Data?, to id: String) async -> Bool {
        guard let body else {
            log.error("\(path, privacy: .public) too large for \(id, privacy: .public)")
            return false
        }
        do {
            try await link.ble.send("POST", path, body, to: id)
            return true
        } catch {
            log.info("\(path, privacy: .public) to \(id, privacy: .public) failed: \(String(describing: error), privacy: .public)")
            return false
        }
    }
}
