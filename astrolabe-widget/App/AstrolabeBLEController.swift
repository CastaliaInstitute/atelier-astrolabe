import CoreBluetooth
import CryptoKit
import Foundation
import OSLog
import WidgetKit

enum ConnectionState: Equatable { case discovered, connecting, connected, disconnected }

struct DiscoveredAstrolabe: Identifiable, Equatable {
    let id: UUID
    var name: String
    var rssi: Int
    var state: ConnectionState
    var macAddress: String?
    var face: String?
    var status = "Discovered"
    var screenURL: URL?
    var screenshotFilename: String?
    var lastSeen = Date()

    var identityLine: String {
        [macAddress, id.uuidString].compactMap { $0 }.joined(separator: " · ")
    }
}

private enum GATT {
    // NimBLE declares these little-endian; CoreBluetooth presents the canonical reversed UUID.
    static let settingsService = CBUUID(string: "01000000-5017-0065-6261-6C6F72747341")
    static let legacyRemoteService = CBUUID(string: "08000000-5017-0065-6261-6C6F72747341")
    static let settingsJSON = CBUUID(string: "03000000-5017-0065-6261-6C6F72747341")
    static let command = CBUUID(string: "04000000-5017-0065-6261-6C6F72747341")
    static let challenge = CBUUID(string: "05000000-5017-0065-6261-6C6F72747341")
    static let status = CBUUID(string: "06000000-5017-0065-6261-6C6F72747341")
    static let advertisedServices = [settingsService, legacyRemoteService]
}

@MainActor
final class AstrolabeBLEController: NSObject, ObservableObject {
    private static let preferredDeviceIDKey = "preferredAstrolabeDeviceID"
    private let logger = Logger(subsystem: "institute.castalia.astrolabe-widget", category: "BLE")

    @Published var devices: [DiscoveredAstrolabe] = []
    @Published var selectedDeviceID: UUID?
    @Published var isScanning = false
    @Published var message: String?

    private var central: CBCentralManager!
    private var peripherals: [UUID: CBPeripheral] = [:]
    private var characteristics: [UUID: [CBUUID: CBCharacteristic]] = [:]
    private var pendingCommands: [UUID: PendingCommand] = [:]
    private var pendingWidgetActions: [UUID: WidgetAction] = [:]
    private var screenshots: [UUID: PlatformImage] = [:]
    private var connectionTimeouts: [UUID: Task<Void, Never>] = [:]

    override init() {
        super.init()
        if let value = UserDefaults.standard.string(forKey: Self.preferredDeviceIDKey) {
            selectedDeviceID = UUID(uuidString: value)
        }
        central = CBCentralManager(delegate: self, queue: .main)
    }

    var selectedDevice: DiscoveredAstrolabe? {
        devices.first { $0.id == selectedDeviceID }
    }

    func screenshot(for id: UUID) -> PlatformImage? { screenshots[id] }

    private func recordDiagnostic(_ value: String) {
        UserDefaults.standard.set(value, forKey: "lastBLEDiagnostic")
        UserDefaults.standard.set(Date().timeIntervalSince1970, forKey: "lastBLEDiagnosticAt")
    }

    func startScanning() {
        recordDiagnostic("scan-request:state=\(central.state.rawValue):auth=\(CBManager.authorization.rawValue)")
        guard central.state == .poweredOn else { message = "Bluetooth is not ready."; return }
        isScanning = true
        central.scanForPeripherals(withServices: nil, options: [CBCentralManagerScanOptionAllowDuplicatesKey: true])
        recordDiagnostic("scanning")
    }

    func stopScanning() { central.stopScan(); isScanning = false }

    func connect(_ id: UUID) {
        guard let peripheral = peripherals[id],
              peripheral.state != .connected,
              peripheral.state != .connecting else { return }
        selectedDeviceID = id
        UserDefaults.standard.set(id.uuidString, forKey: Self.preferredDeviceIDKey)
        mutate(id) { $0.state = .connecting; $0.status = "Connecting" }
        peripheral.delegate = self
        logger.notice("Connecting to preferred peripheral \(id.uuidString, privacy: .public)")
        recordDiagnostic("connecting:\(id.uuidString)")
        central.connect(peripheral)
        connectionTimeouts[id]?.cancel()
        connectionTimeouts[id] = Task { [weak self, weak peripheral] in
            try? await Task.sleep(for: .seconds(15))
            guard !Task.isCancelled, let self, let peripheral,
                  peripheral.state == .connecting else { return }
            self.logger.error("Connection timed out for \(id.uuidString, privacy: .public)")
            self.recordDiagnostic("timeout:\(id.uuidString)")
            self.central.cancelPeripheralConnection(peripheral)
            self.mutate(id) { $0.state = .disconnected; $0.status = "Connection timed out" }
            self.message = "Bluetooth connection timed out."
            self.persistSnapshots()
        }
    }

    func disconnect(_ id: UUID) {
        if let peripheral = peripherals[id] { central.cancelPeripheralConnection(peripheral) }
    }

    func sendFace(_ face: String, to id: UUID, secretHex: String) {
        beginCommand(.structured(["cmd": "face", "face": face]), to: id, secretHex: secretHex)
    }

    func sendGesture(_ gesture: String, to id: UUID, secretHex: String) {
        let parts = gesture.split(separator: " ", maxSplits: 1).map(String.init)
        var body = ["cmd": parts[0]]
        if parts.count > 1 { body["direction"] = parts[1] }
        beginCommand(.structured(body), to: id, secretHex: secretHex)
    }

    func sendConsoleCommand(_ command: String, to id: UUID, secretHex: String) {
        guard !command.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else { return }
        beginCommand(.structured(["command": command]), to: id, secretHex: secretHex)
    }

    func refreshScreenshot(for id: UUID) {
        guard let device = devices.first(where: { $0.id == id }), let base = device.screenURL else {
            message = "This device has not advertised a reachable Wi-Fi URL."
            return
        }
        var components = URLComponents(url: base, resolvingAgainstBaseURL: false)
        components?.path = "/screen.bmp"
        components?.query = nil
        guard let url = components?.url else { return }
        message = "Fetching \(url.host ?? "Astrolabe")/screen.bmp…"
        Task {
            do {
                let (data, response) = try await URLSession.shared.data(from: url)
                guard (response as? HTTPURLResponse)?.statusCode == 200, data.starts(with: [0x42, 0x4d]), let image = PlatformImage(data: data) else {
                    throw URLError(.cannotDecodeContentData)
                }
                let filename = try SharedSnapshotStore.saveScreenshot(data, for: id)
                screenshots[id] = image
                mutate(id) { $0.screenshotFilename = filename; $0.status = "Screen updated" }
                persistSnapshots()
                message = "Screenshot updated."
            } catch {
                message = "Screenshot failed: \(error.localizedDescription)"
            }
        }
    }

    func handle(url: URL) {
        guard url.scheme == "astrolabe-widget",
              let idText = URLComponents(url: url, resolvingAgainstBaseURL: false)?.queryItems?.first(where: { $0.name == "id" })?.value,
              let id = UUID(uuidString: idText) else { return }
        selectedDeviceID = id
        let components = URLComponents(url: url, resolvingAgainstBaseURL: false)
        let action: WidgetAction
        switch url.host {
        case "refresh": action = .refresh
        case "tap": action = .gesture("tap")
        case "swipe":
            let direction = components?.queryItems?.first(where: { $0.name == "direction" })?.value ?? "right"
            action = .gesture("swipe \(direction)")
        default: return
        }
        performOrQueue(action, for: id)
    }

    private func beginCommand(_ command: PendingCommand.Command, to id: UUID, secretHex: String) {
        guard let secret = Data(hex: secretHex), !secret.isEmpty else { message = "Enter the device secret as hexadecimal."; return }
        DeviceSecretStore.save(secret, for: id)
        beginCommand(command, to: id, secret: secret)
    }

    private func beginCommand(_ command: PendingCommand.Command, to id: UUID, secret: Data) {
        guard let peripheral = peripherals[id], peripheral.state == .connected,
              let challenge = characteristics[id]?[GATT.challenge], characteristics[id]?[GATT.command] != nil else {
            message = "Connect and discover the authenticated remote characteristics first."
            return
        }
        pendingCommands[id] = PendingCommand(command: command, secret: secret)
        peripheral.readValue(for: challenge)
    }

    private func performOrQueue(_ action: WidgetAction, for id: UUID) {
        if case .refresh = action, devices.first(where: { $0.id == id })?.screenURL != nil {
            refreshScreenshot(for: id)
            return
        }
        guard let secret = DeviceSecretStore.load(for: id) else {
            message = "Open the device in the app once and enter its secret before using widget controls."
            return
        }
        if peripherals[id]?.state == .connected, characteristics[id]?[GATT.challenge] != nil {
            execute(action, for: id, secret: secret)
            return
        }
        pendingWidgetActions[id] = action
        if peripherals[id] == nil, let peripheral = central.retrievePeripherals(withIdentifiers: [id]).first {
            peripherals[id] = peripheral
            if !devices.contains(where: { $0.id == id }) {
                devices.append(DiscoveredAstrolabe(id: id, name: "Astrolabe", rssi: 0, state: .discovered))
            }
        }
        connect(id)
    }

    private func execute(_ action: WidgetAction, for id: UUID, secret: Data) {
        switch action {
        case .refresh: refreshScreenshot(for: id)
        case .gesture(let gesture):
            let parts = gesture.split(separator: " ", maxSplits: 1).map(String.init)
            var body = ["cmd": parts[0]]
            if parts.count > 1 { body["direction"] = parts[1] }
            beginCommand(.structured(body), to: id, secret: secret)
        }
    }

    private func executePendingWidgetAction(for id: UUID) {
        guard let action = pendingWidgetActions.removeValue(forKey: id), let secret = DeviceSecretStore.load(for: id) else { return }
        execute(action, for: id, secret: secret)
    }

    private func handleChallenge(_ data: Data, peripheral: CBPeripheral) {
        guard var pending = pendingCommands[peripheral.identifier],
              let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let nonce = json["nonce"] as? String,
              let mac = json["mac"] as? String,
              let characteristic = characteristics[peripheral.identifier]?[GATT.command] else {
            message = "The challenge response was invalid."
            return
        }
        mutate(peripheral.identifier) { $0.macAddress = mac }
        let channel = "astrolabe-faculty-amoled175"
        let signed = Data("\(mac)\n\(nonce)\n\(channel)\n".utf8)
        let hmac = HMAC<SHA256>.authenticationCode(for: signed, using: SymmetricKey(data: pending.secret))
        pending.body["nonce"] = nonce
        pending.body["signature"] = hmac.map { String(format: "%02x", $0) }.joined()
        guard let payload = try? JSONSerialization.data(withJSONObject: pending.body) else { return }
        guard payload.count <= peripheral.maximumWriteValueLength(for: .withResponse) else {
            message = "Command exceeds the current single-write GATT limit."
            return
        }
        peripheral.writeValue(payload, for: characteristic, type: .withResponse)
    }

    private func mutate(_ id: UUID, _ body: (inout DiscoveredAstrolabe) -> Void) {
        guard let index = devices.firstIndex(where: { $0.id == id }) else { return }
        body(&devices[index])
    }

    private func persistSnapshots() {
        let values = devices.map { device in
            AstrolabeSnapshot(id: device.id, name: device.name, macAddress: device.macAddress, face: device.face, status: device.status, screenshotFilename: device.screenshotFilename, updatedAt: .now)
        }
        try? SharedSnapshotStore.save(values)
        WidgetCenter.shared.reloadTimelines(ofKind: "AstrolabeWidget")
    }
}

extension AstrolabeBLEController: CBCentralManagerDelegate, CBPeripheralDelegate {
    nonisolated func centralManagerDidUpdateState(_ central: CBCentralManager) {
        Task { @MainActor in
            recordDiagnostic("central-state:\(central.state.rawValue):auth=\(CBManager.authorization.rawValue)")
            if central.state == .poweredOn { startScanning() }
            else { isScanning = false; message = "Bluetooth state: \(central.state.rawValue)" }
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral, advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let advertised = (advertisementData[CBAdvertisementDataServiceUUIDsKey] as? [CBUUID]) ?? []
        let name = (advertisementData[CBAdvertisementDataLocalNameKey] as? String) ?? peripheral.name ?? "Astrolabe"
        guard advertised.contains(where: GATT.advertisedServices.contains) || name.localizedCaseInsensitiveContains("Astrolabe") else { return }
        Task { @MainActor in
            recordDiagnostic("discovered:\(peripheral.identifier.uuidString):\(name):rssi=\(RSSI.intValue)")
            peripherals[peripheral.identifier] = peripheral
            if let index = devices.firstIndex(where: { $0.id == peripheral.identifier }) {
                devices[index].rssi = RSSI.intValue; devices[index].lastSeen = .now
            } else {
                devices.append(DiscoveredAstrolabe(id: peripheral.identifier, name: name, rssi: RSSI.intValue, state: .discovered))
                devices.sort { $0.rssi > $1.rssi }
                selectedDeviceID = selectedDeviceID ?? peripheral.identifier
            }
            if selectedDeviceID == peripheral.identifier,
               peripheral.state != .connected,
               peripheral.state != .connecting {
                connect(peripheral.identifier)
            }
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        Task { @MainActor in
            connectionTimeouts.removeValue(forKey: peripheral.identifier)?.cancel()
            logger.notice("Connected to \(peripheral.identifier.uuidString, privacy: .public); discovering GATT")
            recordDiagnostic("connected-discovering-gatt:\(peripheral.identifier.uuidString)")
            mutate(peripheral.identifier) { $0.state = .connected; $0.status = "Discovering GATT services" }
            peripheral.discoverServices(nil)
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        Task { @MainActor in
            connectionTimeouts.removeValue(forKey: peripheral.identifier)?.cancel()
            logger.error("Connection failed for \(peripheral.identifier.uuidString, privacy: .public): \(error?.localizedDescription ?? "unknown", privacy: .public)")
            recordDiagnostic("failed:\(peripheral.identifier.uuidString):\(error?.localizedDescription ?? "unknown")")
            mutate(peripheral.identifier) {
                $0.state = .disconnected
                $0.status = error?.localizedDescription ?? "Connection failed"
            }
            message = "Could not connect to \(peripheral.name ?? "Astrolabe"): \(error?.localizedDescription ?? "unknown error")"
            persistSnapshots()
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        Task { @MainActor in
            connectionTimeouts.removeValue(forKey: peripheral.identifier)?.cancel()
            logger.notice("Disconnected from \(peripheral.identifier.uuidString, privacy: .public): \(error?.localizedDescription ?? "no error", privacy: .public)")
            recordDiagnostic("disconnected:\(peripheral.identifier.uuidString):\(error?.localizedDescription ?? "no error")")
            mutate(peripheral.identifier) { $0.state = .disconnected; $0.status = error?.localizedDescription ?? "Disconnected" }
            persistSnapshots()
        }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        Task { @MainActor in
            guard error == nil else { message = error?.localizedDescription; return }
            logger.notice("Discovered \(peripheral.services?.count ?? 0) GATT services for \(peripheral.identifier.uuidString, privacy: .public)")
            recordDiagnostic("gatt-services:\(peripheral.identifier.uuidString):\(peripheral.services?.count ?? 0)")
            for service in peripheral.services ?? [] { peripheral.discoverCharacteristics(nil, for: service) }
        }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        Task { @MainActor in
            guard error == nil else { message = error?.localizedDescription; return }
            var map = characteristics[peripheral.identifier] ?? [:]
            for item in service.characteristics ?? [] { map[item.uuid] = item }
            characteristics[peripheral.identifier] = map
            if let settings = map[GATT.settingsJSON] { peripheral.readValue(for: settings) }
            if let status = map[GATT.status] { peripheral.readValue(for: status) }
            mutate(peripheral.identifier) { $0.status = map[GATT.command] == nil ? "Connected; remote control unavailable" : "Connected" }
            persistSnapshots()
            if map[GATT.challenge] != nil { executePendingWidgetAction(for: peripheral.identifier) }
        }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        Task { @MainActor in
            guard error == nil, let data = characteristic.value else { message = error?.localizedDescription; return }
            if characteristic.uuid == GATT.challenge { handleChallenge(data, peripheral: peripheral) }
            else if characteristic.uuid == GATT.settingsJSON,
                    let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
                    let wifi = json["wifi"] as? [String: Any], let text = wifi["url"] as? String, let url = URL(string: text), !text.isEmpty {
                mutate(peripheral.identifier) { $0.screenURL = url }
                refreshScreenshot(for: peripheral.identifier)
            } else if characteristic.uuid == GATT.status, let text = String(data: data, encoding: .utf8) {
                mutate(peripheral.identifier) { $0.status = text }
                persistSnapshots()
            }
        }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        Task { @MainActor in
            pendingCommands[peripheral.identifier] = nil
            if let error { message = "BLE command failed: \(error.localizedDescription)" }
            else { message = "BLE command accepted."; if let status = characteristics[peripheral.identifier]?[GATT.status] { peripheral.readValue(for: status) } }
        }
    }
}

private struct PendingCommand {
    enum Command { case structured([String: String]) }
    var body: [String: Any]
    let secret: Data
    init(command: Command, secret: Data) {
        switch command { case .structured(let values): body = values }
        self.secret = secret
    }
}

private enum WidgetAction {
    case refresh
    case gesture(String)
}
