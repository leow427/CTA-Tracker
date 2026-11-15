/// What the app should do after each `GET /info` poll of one device. Pure: the app does the requests.
public struct DeviceWatch: Sendable {
    public enum Action: Equatable, Sendable { case fetchConfig, sendNearby, resendState, wentOffline, cameOnline, docked }

    /// Missed polls in a row before a device counts as gone (about 6 s at the 2 s poll; spec 5.9).
    static let missesToOffline = 3

    private var last: InfoMessage?   // nil until online, and again once offline
    private var misses = 0

    public init() {}

    /// `info` nil means the request failed.
    public mutating func polled(_ info: InfoMessage?) -> [Action] {
        guard let info else {
            guard last != nil else { return [] }
            misses += 1
            guard misses == Self.missesToOffline else { return [] }
            last = nil
            return [.wentOffline]
        }
        misses = 0
        defer { last = info }
        // A dock may have been picked on while we weren't looking.
        guard let last else { return info.type == .dock ? [.cameOnline, .resendState, .fetchConfig] : [.cameOnline, .resendState] }
        var actions: [Action] = []
        let rebooted = info.uptime < last.uptime
        if rebooted { actions.append(.resendState) }   // the resend covers nearby, so skip the wake check
        if info.picksVer != last.picksVer { actions.append(.fetchConfig) }
        if !rebooted && info.wake != last.wake { actions.append(.sendNearby) }
        if info.docked && !last.docked { actions.append(.docked) }   // the wearable drops its pin (spec 5.6)
        return actions
    }
}
