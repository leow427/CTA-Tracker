import Foundation

/// The Mapbox Standard style's light presets (redesign spec 6).
public enum LightPreset: String, Sendable {
    case dawn, day, dusk, night
}

/// Sunrise and sunset on the phone (NOAA's sunrise equation), so the map's light needs no network.
public enum SunTimes {
    /// Sunrise and sunset for the solar day containing `day` at the place, or nil when the sun doesn't rise or set.
    public static func riseSet(on day: Date, lat: Double, lon: Double) -> (rise: Date, set: Date)? {
        var utc = Calendar(identifier: .gregorian)
        utc.timeZone = TimeZone(identifier: "UTC")!
        // The place's solar day, so a Chicago evening (already tomorrow in UTC) still counts as today.
        let solar = day.addingTimeInterval(lon / 15 * 3600)
        guard let n = utc.ordinality(of: .day, in: .year, for: solar).map(Double.init) else { return nil }
        let midnight = utc.startOfDay(for: solar)
        guard let rise = hour(n, lat: lat, lon: lon, rising: true),
              let set = hour(n, lat: lat, lon: lon, rising: false) else { return nil }
        let r = midnight.addingTimeInterval(rise * 3600)
        var s = midnight.addingTimeInterval(set * 3600)
        if s < r { s.addTimeInterval(86_400) }   // west of Greenwich, sunset is often past UTC midnight
        return (r, s)
    }

    /// Dawn and dusk within `Config.twilightMin` of sunrise and sunset, day between, night otherwise.
    public static func preset(at now: Date, rise: Date, set: Date) -> LightPreset {
        let w = Config.twilightMin * 60
        if abs(now.timeIntervalSince(rise)) <= w { return .dawn }
        if abs(now.timeIntervalSince(set)) <= w { return .dusk }
        return now > rise && now < set ? .day : .night
    }

    /// The UTC hour of sunrise or sunset on day-of-year `n`, with the official zenith of 90.833°.
    private static func hour(_ n: Double, lat: Double, lon: Double, rising: Bool) -> Double? {
        let rad = Double.pi / 180
        let lngHour = lon / 15
        let t = n + ((rising ? 6 : 18) - lngHour) / 24
        let m = 0.9856 * t - 3.289
        let l = (m + 1.916 * sin(m * rad) + 0.020 * sin(2 * m * rad) + 282.634).truncatingRemainder(dividingBy: 360)
        var ra = (atan(0.91764 * tan(l * rad)) / rad + 360).truncatingRemainder(dividingBy: 360)
        ra += (floor(l / 90) - floor(ra / 90)) * 90   // same quadrant as l
        ra /= 15
        let sinDec = 0.39782 * sin(l * rad)
        let cosDec = cos(asin(sinDec))
        let cosH = (cos(90.833 * rad) - sinDec * sin(lat * rad)) / (cosDec * cos(lat * rad))
        guard (-1...1).contains(cosH) else { return nil }
        let h = (rising ? 360 - acos(cosH) / rad : acos(cosH) / rad) / 15
        let local = h + ra - 0.06571 * t - 6.622
        return ((local - lngHour).truncatingRemainder(dividingBy: 24) + 24).truncatingRemainder(dividingBy: 24)
    }
}
