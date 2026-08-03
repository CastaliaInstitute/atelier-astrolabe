import SwiftUI
import WidgetKit

struct AstrolabeEntry: TimelineEntry {
    let date: Date
    let snapshot: AstrolabeSnapshot
    let image: PlatformImage?
}

struct AstrolabeProvider: TimelineProvider {
    func placeholder(in context: Context) -> AstrolabeEntry { entry(for: .placeholder) }
    func getSnapshot(in context: Context, completion: @escaping (AstrolabeEntry) -> Void) {
        completion(entry(for: SharedSnapshotStore.load().first ?? .placeholder))
    }
    func getTimeline(in context: Context, completion: @escaping (Timeline<AstrolabeEntry>) -> Void) {
        let snapshot = SharedSnapshotStore.load().first ?? .placeholder
        completion(Timeline(entries: [entry(for: snapshot)], policy: .after(.now.addingTimeInterval(15 * 60))))
    }
    private func entry(for snapshot: AstrolabeSnapshot) -> AstrolabeEntry {
        let image = SharedSnapshotStore.screenshotData(named: snapshot.screenshotFilename).flatMap(PlatformImage.init(data:))
        return AstrolabeEntry(date: .now, snapshot: snapshot, image: image)
    }
}

struct AstrolabeWidgetView: View {
    @Environment(\.widgetFamily) private var family
    let entry: AstrolabeEntry

    var body: some View {
        ZStack(alignment: .bottom) {
            if let image = entry.image {
                Image(platformImage: image).resizable().scaledToFill()
            } else {
                LinearGradient(colors: [.black, Color(red: 0.18, green: 0.12, blue: 0.04)], startPoint: .top, endPoint: .bottom)
                Image(systemName: "circle.hexagongrid.fill").font(.system(size: 52)).foregroundStyle(.yellow.opacity(0.65))
            }
            HStack {
                VStack(alignment: .leading, spacing: 2) {
                    Text(entry.snapshot.name).font(.headline)
                    Text(entry.snapshot.face ?? entry.snapshot.status).font(.caption).lineLimit(1)
                }
                Spacer()
                HStack(spacing: 10) {
                    Link(destination: controlURL("swipe", extra: "&direction=left")) {
                        Image(systemName: "chevron.left.circle.fill")
                    }
                    Link(destination: controlURL("tap")) {
                        Image(systemName: "hand.tap.fill")
                    }
                    Link(destination: controlURL("swipe", extra: "&direction=right")) {
                        Image(systemName: "chevron.right.circle.fill")
                    }
                    Link(destination: controlURL("refresh")) {
                        Image(systemName: "arrow.clockwise.circle.fill")
                    }
                    .font(.title2)
                }
            }
            .padding(10)
            .background(.black.opacity(0.66))
            .foregroundStyle(.white)
        }
        .containerBackground(.black, for: .widget)
    }

    private func controlURL(_ action: String, extra: String = "") -> URL {
        URL(string: "astrolabe-widget://\(action)?id=\(entry.snapshot.id.uuidString)\(extra)")!
    }
}

struct AstrolabeWidget: Widget {
    let kind = "AstrolabeWidget"
    var body: some WidgetConfiguration {
        StaticConfiguration(kind: kind, provider: AstrolabeProvider()) { entry in AstrolabeWidgetView(entry: entry) }
            .configurationDisplayName("Astrolabe Remote")
            .description("See the current screen and open BLE controls for your Astrolabe.")
            .supportedFamilies([.systemSmall, .systemMedium, .systemLarge])
    }
}

@main
struct AstrolabeWidgetBundle: WidgetBundle {
    var body: some Widget { AstrolabeWidget() }
}
