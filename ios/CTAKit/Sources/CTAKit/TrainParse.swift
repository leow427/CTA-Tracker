import Foundation

/// Train Tracker responses (`outputType=JSON`).
public enum TrainParse {
    /// One prediction per `eta`, timed from `fetched` (when the response arrived) plus `arrT − tmst`.
    /// Throws on an API error (`errCd` ≠ "0") or malformed JSON.
    public static func arrivals(_ data: Data, fetched: Date) throws -> [Prediction] {
        let b = try body(data)
        guard let etas = b.eta?.items else { return [] }
        guard let tmst = b.tmst, CTATime.naive(tmst) != nil else { throw CTAError.api(code: "tmst", message: "No response time") }
        return etas.compactMap { e in
            guard let at = CTATime.at(e.arrT, responseTime: tmst, fetched: fetched) else { return nil }
            return Prediction(stopID: e.stpId, route: e.rt, dir: "", dest: e.destNm, at: at,
                              live: e.isSch == "0", delayed: e.isDly == "1")
        }
    }

    /// Trains on the line (ttpositions). `train` may be one object instead of an array.
    public static func positions(_ data: Data) throws -> [Vehicle] {
        let trains = try body(data).route?.items.flatMap { $0.train?.items ?? [] } ?? []
        return trains.compactMap { t in
            guard let (lat, lon) = validCoordinate(t.lat, t.lon) else { return nil }
            return Vehicle(id: t.rn, lat: lat, lon: lon, heading: t.heading?.value ?? 0,
                           dest: t.destNm, nextStopID: t.nextStpId ?? "", nextStop: t.nextStaNm ?? "",
                           pid: nil, pdist: nil)
        }
    }

    private static func body(_ data: Data) throws -> Response.Body {
        let b = try JSONDecoder().decode(Response.self, from: data).ctatt
        guard b.errCd == "0" else { throw CTAError.api(code: b.errCd, message: b.errNm ?? "") }
        return b
    }

    private struct Response: Decodable {
        let ctatt: Body
        struct Body: Decodable {
            let tmst: String?
            let errCd: String
            let errNm: String?
            let eta: OneOrMany<ETA>?
            let route: OneOrMany<Route>?
        }
        struct ETA: Decodable {
            let stpId: String, rt: String, destNm: String, arrT: String, isSch: String, isDly: String
        }
        struct Route: Decodable { let train: OneOrMany<Train>? }
        struct Train: Decodable {
            let rn: String, destNm: String
            let nextStaNm: String?, nextStpId: String?
            let lat: Loose<Double>?, lon: Loose<Double>?, heading: Loose<Double>?
        }
    }
}
