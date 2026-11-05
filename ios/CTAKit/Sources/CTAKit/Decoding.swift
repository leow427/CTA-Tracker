import Foundation

/// CTA sends a single object where an array is expected when a list has one item.
struct OneOrMany<T: Decodable>: Decodable {
    let items: [T]
    init(from decoder: any Decoder) throws {
        let c = try decoder.singleValueContainer()
        if let many = try? c.decode([T].self) { items = many } else { items = [try c.decode(T.self)] }
    }
}

/// A number CTA may send as a number or a string (Bus Tracker `pid` and `pdist`, Train Tracker `lat`).
/// Junk ("" or "n/a") becomes nil, so one bad vehicle doesn't fail the whole response.
struct Loose<T: Decodable & LosslessStringConvertible>: Decodable {
    let value: T?
    init(from decoder: any Decoder) throws {
        let c = try decoder.singleValueContainer()
        value = (try? c.decode(T.self)) ?? (try? c.decode(String.self)).flatMap(T.init)
    }
}

/// A coordinate that's actually on Earth (rejects missing values, NaN, and junk).
func validCoordinate(_ lat: Loose<Double>?, _ lon: Loose<Double>?) -> (Double, Double)? {
    guard let lat = lat?.value, let lon = lon?.value, (-90...90).contains(lat), (-180...180).contains(lon) else { return nil }
    return (lat, lon)
}
