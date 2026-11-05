import Foundation

/// Base spec 6.3. Mirrors the firmware's stopMood / deviceMood.
public enum MoodRules {
    public static func stopMood(_ r: StopRecord, now: Date, night: Bool) -> Mood {
        if isStale(r, now: now) || night { return .sleepy }
        if r.alert || r.arrivals.isEmpty { return .sad }
        if r.arrivals[0].delayed { return .worried }
        return .happy   // timetable-only arrivals count as Happy (plan 1 decision 2)
    }

    /// `canHype` marks the stops that may make the pet Hyped: the 3 closest on the handheld and in the app.
    public static func deviceMood(_ stops: [(record: StopRecord, canHype: Bool)], now: Date,
                                  night: Bool, hasLocation: Bool) -> Mood {
        if !hasLocation || night { return .sleepy }
        let hyped = stops.contains { s in
            guard s.canHype, !isStale(s.record, now: now), let next = s.record.arrivals.first else { return false }
            return next.live && next.min <= Config.hypedMaxMin
        }
        if hyped { return .hyped }
        let moods = stops.map { stopMood($0.record, now: now, night: false) }.filter { $0 != .sleepy }
        guard !moods.isEmpty else { return .sleepy }
        // Majority; ties go to the more serious mood.
        var best = Mood.sleepy, bestCount = 0
        for mood in [Mood.sad, .worried, .happy] {
            let n = moods.count { $0 == mood }
            if n > bestCount { best = mood; bestCount = n }
        }
        return best
    }

    /// Never fetched, or fetched more than 5 min ago.
    static func isStale(_ r: StopRecord, now: Date) -> Bool {
        r.ts == 0 || now.timeIntervalSince1970 - Double(r.ts) > Config.staleSec
    }
}
