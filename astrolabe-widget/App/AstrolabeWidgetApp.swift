import SwiftUI

@main
struct AstrolabeWidgetApp: App {
    @StateObject private var controller = AstrolabeBLEController()

    var body: some Scene {
        WindowGroup {
            ContentView(controller: controller)
                .onOpenURL { controller.handle(url: $0) }
        }
#if os(macOS)
        .defaultSize(width: 920, height: 640)
        .commands {
            CommandGroup(after: .newItem) {
                Button(controller.isScanning ? "Stop Scanning" : "Scan for Astrolabes") {
                    controller.isScanning ? controller.stopScanning() : controller.startScanning()
                }
                .keyboardShortcut("r", modifiers: [.command, .shift])
            }
        }
#endif
    }
}
