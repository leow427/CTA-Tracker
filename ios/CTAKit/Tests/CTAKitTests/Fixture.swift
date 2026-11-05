import Foundation
@testable import CTAKit

/// Reads `fixtures/cta/<name>` from the repo root, the same files the C++ tests read.
func fixture(_ name: String) throws -> Data {
    let url = URL(filePath: #filePath).deletingLastPathComponent()
        .appending(path: "../../../../fixtures/cta/\(name)").standardized
    return try Data(contentsOf: url)
}

/// A CTA wall-clock time in Chicago, as the API writes it.
func chicago(_ s: String) -> Date {
    let f = DateFormatter()
    f.locale = Locale(identifier: "en_US_POSIX")
    f.timeZone = TimeZone(identifier: "America/Chicago")
    f.dateFormat = s.contains("T") ? "yyyy-MM-dd'T'HH:mm:ss" : "yyyyMMdd HH:mm"
    return f.date(from: s)!
}

/// One record per stop, in order of first appearance: the C++ parsers' grouping.
func records(_ preds: [Prediction], kind: Kind, names: [String: String] = [:], fetched: Date) -> [StopRecord] {
    var order: [String] = []
    for p in preds where !order.contains(p.stopID) { order.append(p.stopID) }
    return order.map { id in
        StopRecord.make(id: id, kind: kind, name: names[id] ?? "",
                        from: preds.filter { $0.stopID == id }, alert: false, fetched: fetched)
    }
}
