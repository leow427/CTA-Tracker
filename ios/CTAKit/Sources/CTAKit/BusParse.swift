import Foundation

/// Bus Tracker v2 responses (`format=json`).
public enum BusParse {
    /// One prediction per `prd`, timed from `fetched` plus `prdtm − tmstmp`. "DUE" is `fetched`; "DLY" is delayed.
    /// No predictions at a stop is an empty list; an error not tied to a stop or route (bad key, limit) throws.
    public static func predictions(_ data: Data, fetched: Date) throws -> [Prediction] {
        try list(data, \.prd).compactMap { p in
            guard let at = p.prdctdn == "DUE" ? fetched : CTATime.at(p.prdtm, responseTime: p.tmstmp, fetched: fetched)
            else { return nil }
            return Prediction(stopID: p.stpid, route: p.rt, dir: p.rtdir, dest: p.des, at: at,
                              live: true, delayed: p.dly || p.prdctdn == "DLY")
        }
    }

    public static func vehicles(_ data: Data) throws -> [Vehicle] {
        try list(data, \.vehicle).compactMap { v in
            guard let (lat, lon) = validCoordinate(v.lat, v.lon) else { return nil }
            return Vehicle(id: v.vid, lat: lat, lon: lon, heading: v.hdg?.value ?? 0, dest: v.des ?? "",
                           nextStopID: "", nextStop: "", pid: v.pid?.value, pdist: v.pdist?.value)
        }
    }

    public static func patterns(_ data: Data) throws -> [Pattern] {
        try list(data, \.ptr).compactMap { p -> Pattern? in
            guard let pid = p.pid.value else { return nil }
            return Pattern(pid: pid, dir: p.rtdir ?? "", points: p.pt.items.compactMap { pt in
                guard let (lat, lon) = validCoordinate(pt.lat, pt.lon) else { return nil }
                let stop = pt.typ == "S"
                return Pattern.Point(lat: lat, lon: lon, stopID: stop ? pt.stpid : nil,
                                     stopName: stop ? pt.stpnm : nil, pdist: pt.pdist?.value ?? 0)
            })
        }
    }

    private static func list<T>(_ data: Data, _ key: KeyPath<Response.Body, OneOrMany<T>?>) throws -> [T] {
        let b = try JSONDecoder().decode(Response.self, from: data).body
        if let items = b[keyPath: key]?.items { return items }
        // Errors that name a stop, route, or pattern just mean "nothing there right now".
        if let e = b.error?.items.first(where: { $0.stpid == nil && $0.rt == nil && $0.pid == nil }) {
            throw CTAError.api(code: "bus", message: e.msg)
        }
        return []
    }

    private struct Response: Decodable {
        let body: Body
        enum CodingKeys: String, CodingKey { case body = "bustime-response" }

        struct Body: Decodable {
            let prd: OneOrMany<PRD>?
            let vehicle: OneOrMany<Veh>?
            let ptr: OneOrMany<PTR>?
            let error: OneOrMany<APIError>?
        }
        struct PRD: Decodable {
            let tmstmp: String, stpid: String, rt: String, rtdir: String, des: String, prdtm: String, prdctdn: String
            let dly: Bool
        }
        struct Veh: Decodable {
            let vid: String, des: String?
            let lat: Loose<Double>?, lon: Loose<Double>?, hdg: Loose<Double>?, pid: Loose<Int>?, pdist: Loose<Double>?
        }
        struct PTR: Decodable {
            let pid: Loose<Int>
            let rtdir: String?
            let pt: OneOrMany<PT>
        }
        struct PT: Decodable {
            let lat: Loose<Double>?, lon: Loose<Double>?, typ: String
            let stpid: String?, stpnm: String?, pdist: Loose<Double>?
        }
        struct APIError: Decodable {
            let msg: String
            let stpid: String?, rt: String?, pid: Loose<Int>?
        }
    }
}
