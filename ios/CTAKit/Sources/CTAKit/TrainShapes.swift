import Foundation

/// One drawn path of a train line, from `train_shapes.json` (app spec 7.3). Each point is `[lat, lon]`.
public struct TrainShape: Decodable, Sendable {
    public let route: String
    public let points: [[Double]]
}

public enum TrainShapes {
    public static func decode(_ data: Data) throws -> [TrainShape] {
        try JSONDecoder().decode([TrainShape].self, from: data)
    }
}
