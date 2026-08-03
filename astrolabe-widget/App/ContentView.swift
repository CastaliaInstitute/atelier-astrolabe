import SwiftUI

struct ContentView: View {
    @ObservedObject var controller: AstrolabeBLEController
    @State private var secret = ""
    @State private var face = "moon"
    @State private var command = ""

    var body: some View {
        NavigationSplitView {
            List(controller.devices, selection: $controller.selectedDeviceID) { device in
                VStack(alignment: .leading, spacing: 3) {
                    Text(device.name).font(.headline)
                    Text("RSSI \(device.rssi) dBm · \(device.state.label)")
                        .font(.caption).foregroundStyle(.secondary)
                }
                .tag(device.id)
            }
            .navigationTitle("Astrolabes")
            .toolbar {
                Button {
                    controller.isScanning ? controller.stopScanning() : controller.startScanning()
                } label: {
                    Label(controller.isScanning ? "Stop" : "Scan", systemImage: controller.isScanning ? "stop.circle" : "dot.radiowaves.left.and.right")
                }
            }
        } detail: {
            if let device = controller.selectedDevice {
                ScrollView {
                    VStack(alignment: .leading, spacing: 18) {
                        HStack {
                            VStack(alignment: .leading) {
                                Text(device.name).font(.largeTitle.bold())
                                Text(device.identityLine).foregroundStyle(.secondary)
                            }
                            Spacer()
                            if device.state == .connected {
                                Button("Disconnect") { controller.disconnect(device.id) }
                            } else {
                                Button("Connect") { controller.connect(device.id) }.buttonStyle(.borderedProminent)
                            }
                        }

                        screen(for: device)

                        GroupBox("Authenticated BLE control") {
                            VStack(alignment: .leading, spacing: 12) {
                                SecureField("Device secret (hex)", text: $secret)
                                    .textFieldStyle(.roundedBorder)
                                HStack {
                                    TextField("Face slug", text: $face)
                                    Button("Switch Face") {
                                        controller.sendFace(face, to: device.id, secretHex: secret)
                                    }
                                    Button("Tap") { controller.sendGesture("tap", to: device.id, secretHex: secret) }
                                    Menu("Swipe") {
                                        ForEach(["left", "right", "up", "down"], id: \.self) { direction in
                                            Button(direction.capitalized) { controller.sendGesture("swipe \(direction)", to: device.id, secretHex: secret) }
                                        }
                                    }
                                }
                                HStack {
                                    TextField("Console command", text: $command)
                                        .onSubmit { sendCommand(to: device.id) }
                                    Button("Execute") { sendCommand(to: device.id) }
                                }
                            }.padding(.top, 6)
                        }

                        if let message = controller.message {
                            Text(message).font(.callout).foregroundStyle(.secondary)
                        }
                    }.padding(24)
                }
            } else {
                ContentUnavailableView("No Astrolabe Selected", systemImage: "circle.dotted", description: Text("Scan, then select a device by identity and signal strength."))
            }
        }
        .onAppear { controller.startScanning() }
    }

    @ViewBuilder
    private func screen(for device: DiscoveredAstrolabe) -> some View {
        GroupBox("Current screen") {
            VStack {
                if let image = controller.screenshot(for: device.id) {
                    Image(platformImage: image).resizable().scaledToFit().frame(maxHeight: 300)
                } else {
                    ContentUnavailableView("No capture yet", systemImage: "display", description: Text("Connect over BLE to discover the device's local screen endpoint."))
                        .frame(height: 220)
                }
                HStack {
                    Spacer()
                    Button("Refresh Screenshot") { controller.refreshScreenshot(for: device.id) }
                        .disabled(device.screenURL == nil)
                }
            }.padding(.top, 6)
        }
    }

    private func sendCommand(to id: UUID) {
        controller.sendConsoleCommand(command, to: id, secretHex: secret)
        command = ""
    }
}

extension ConnectionState {
    var label: String {
        switch self {
        case .discovered: "discovered"
        case .connecting: "connecting"
        case .connected: "connected"
        case .disconnected: "disconnected"
        }
    }
}
