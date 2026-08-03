import Foundation

struct AstrolabeSnapshot: Codable, Identifiable, Equatable {
    var id: UUID
    var name: String
    var macAddress: String?
    var face: String?
    var status: String
    var screenshotFilename: String?
    var updatedAt: Date

    static let placeholder = AstrolabeSnapshot(
        id: UUID(uuidString: "00000000-0000-0000-0000-000000000001")!,
        name: "Astrolabe",
        macAddress: nil,
        face: "moon",
        status: "Open the app to connect",
        screenshotFilename: nil,
        updatedAt: .now
    )
}
enum SharedSnapshotStore {
    static let appGroup = "group.institute.castalia.astrolabe-widget"
    private static let snapshotsFilename = "snapshots.json"

    static func load() -> [AstrolabeSnapshot] {
        guard let data = try? Data(contentsOf: snapshotsURL),
              let snapshots = try? JSONDecoder().decode([AstrolabeSnapshot].self, from: data) else {
            return []
        }
        return snapshots
    }

    static func save(_ snapshots: [AstrolabeSnapshot]) throws {
        let data = try JSONEncoder().encode(snapshots)
        try FileManager.default.createDirectory(at: containerURL, withIntermediateDirectories: true)
        try data.write(to: snapshotsURL, options: .atomic)
    }

    static func saveScreenshot(_ data: Data, for id: UUID) throws -> String {
        let filename = "screen-\(id.uuidString).bmp"
        try FileManager.default.createDirectory(at: containerURL, withIntermediateDirectories: true)
        try data.write(to: containerURL.appendingPathComponent(filename), options: .atomic)
        return filename
    }

    static func screenshotData(named filename: String?) -> Data? {
        guard let filename else { return nil }
        return try? Data(contentsOf: containerURL.appendingPathComponent(filename))
    }

    private static var snapshotsURL: URL {
        containerURL.appendingPathComponent(snapshotsFilename)
    }

    private static var containerURL: URL {
        if let url = FileManager.default.containerURL(forSecurityApplicationGroupIdentifier: appGroup) {
            return url
        }
        return FileManager.default.temporaryDirectory.appendingPathComponent(appGroup, isDirectory: true)
    }
}
