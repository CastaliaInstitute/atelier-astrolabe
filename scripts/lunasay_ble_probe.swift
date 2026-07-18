#!/usr/bin/env swift
import CoreBluetooth
import Foundation

// NimBLE's BLE_UUID128_INIT bytes are little-endian; CoreBluetooth uses the
// canonical reversed representation of the firmware UUIDs.
private let settingsServiceUUID = CBUUID(string: "01000000-5017-0065-6261-6C6F72747341")
private let settingsJSONCharacteristicUUID = CBUUID(string: "03000000-5017-0065-6261-6C6F72747341")

enum ProbeOperation: String {
    case advertise
    case settingsRead = "settings-read"
    case settingsRoundtrip = "settings-roundtrip"
}

enum ProbePhase {
    case scanning
    case connecting
    case discoveringServices
    case discoveringCharacteristics
    case readingInitialSettings
    case writingEpoch
    case readingVerifiedSettings
}

final class LunaSayProbe: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var settingsCharacteristic: CBCharacteristic?
    private let needle: String
    private let timeout: TimeInterval
    private let operation: ProbeOperation
    private var phase: ProbePhase = .scanning
    private var finished = false
    private var advertisedName = ""
    private var advertisedRSSI = 0
    private var requestedEpoch = 0
    private var fetchedSummary: [String: Any] = [:]

    init(needle: String, timeout: TimeInterval, operation: ProbeOperation) {
        self.needle = needle.lowercased()
        self.timeout = timeout
        self.operation = operation
        super.init()
        central = CBCentralManager(delegate: self, queue: .main)
        DispatchQueue.main.asyncAfter(deadline: .now() + timeout) { [weak self] in
            self?.fail(code: 2, error: "timeout")
        }
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        guard !finished else { return }
        guard central.state == .poweredOn else {
            if central.state == .unsupported || central.state == .unauthorized || central.state == .poweredOff {
                fail(code: 3, error: "bluetooth-state-\(central.state.rawValue)")
            }
            return
        }
        central.scanForPeripherals(
            withServices: nil,
            options: [CBCentralManagerScanOptionAllowDuplicatesKey: true]
        )
    }

    func centralManager(
        _ central: CBCentralManager,
        didDiscover peripheral: CBPeripheral,
        advertisementData: [String: Any],
        rssi RSSI: NSNumber
    ) {
        guard !finished, phase == .scanning else { return }
        let advertised = advertisementData[CBAdvertisementDataLocalNameKey] as? String
        let name = advertised ?? peripheral.name ?? ""
        guard name.lowercased().contains(needle) else { return }
        advertisedName = name
        advertisedRSSI = RSSI.intValue

        if operation == .advertise {
            finish(code: 0, payload: basePayload(found: true))
            return
        }

        central.stopScan()
        self.peripheral = peripheral
        peripheral.delegate = self
        phase = .connecting
        central.connect(peripheral, options: nil)
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        guard !finished else { return }
        phase = .discoveringServices
        peripheral.discoverServices([settingsServiceUUID])
    }

    func centralManager(
        _ central: CBCentralManager,
        didFailToConnect peripheral: CBPeripheral,
        error: Error?
    ) {
        fail(code: 4, error: "connect-failed: \(error?.localizedDescription ?? "unknown")")
    }

    func centralManager(
        _ central: CBCentralManager,
        didDisconnectPeripheral peripheral: CBPeripheral,
        error: Error?
    ) {
        guard !finished else { return }
        fail(code: 4, error: "disconnected: \(error?.localizedDescription ?? "unexpected")")
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard !finished else { return }
        if let error {
            fail(code: 5, error: "service-discovery-failed: \(error.localizedDescription)")
            return
        }
        guard let service = peripheral.services?.first(where: { $0.uuid == settingsServiceUUID }) else {
            fail(code: 5, error: "settings-service-not-found")
            return
        }
        phase = .discoveringCharacteristics
        peripheral.discoverCharacteristics([settingsJSONCharacteristicUUID], for: service)
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didDiscoverCharacteristicsFor service: CBService,
        error: Error?
    ) {
        guard !finished else { return }
        if let error {
            fail(code: 6, error: "characteristic-discovery-failed: \(error.localizedDescription)")
            return
        }
        guard let characteristic = service.characteristics?.first(where: {
            $0.uuid == settingsJSONCharacteristicUUID
        }) else {
            fail(code: 6, error: "settings-json-characteristic-not-found")
            return
        }
        settingsCharacteristic = characteristic
        phase = .readingInitialSettings
        peripheral.readValue(for: characteristic)
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didUpdateValueFor characteristic: CBCharacteristic,
        error: Error?
    ) {
        guard !finished, characteristic.uuid == settingsJSONCharacteristicUUID else { return }
        if let error {
            fail(code: 7, error: "settings-read-failed: \(error.localizedDescription)")
            return
        }
        guard let data = characteristic.value,
              let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              json["ok"] as? Bool == true,
              json["tz"] is String,
              json["location"] is [String: Any],
              json["wifi"] is [String: Any] else {
            fail(code: 7, error: "invalid-settings-json")
            return
        }

        if phase == .readingInitialSettings {
            let location = json["location"] as? [String: Any] ?? [:]
            let wifi = json["wifi"] as? [String: Any] ?? [:]
            fetchedSummary = [
                "tz": json["tz"] as? String ?? "",
                "location_valid": location["valid"] as? Bool ?? false,
                "wifi_ap": wifi["ap"] as? Bool ?? false,
                "ssid_present": !((wifi["ssid"] as? String) ?? "").isEmpty,
            ]
            if operation == .settingsRead {
                var payload = basePayload(found: true)
                payload["settings_read"] = true
                payload["settings"] = fetchedSummary
                finish(code: 0, payload: payload)
                return
            }
            requestedEpoch = Int(Date().timeIntervalSince1970)
            guard let payload = try? JSONSerialization.data(withJSONObject: ["epoch": requestedEpoch]) else {
                fail(code: 8, error: "epoch-json-encode-failed")
                return
            }
            phase = .writingEpoch
            peripheral.writeValue(payload, for: characteristic, type: .withResponse)
            return
        }

        guard phase == .readingVerifiedSettings,
              let observedEpoch = json["epoch"] as? NSNumber else {
            fail(code: 9, error: "unexpected-settings-read-state")
            return
        }
        let delta = abs(observedEpoch.intValue - requestedEpoch)
        guard delta <= 10 else {
            fail(code: 9, error: "epoch-readback-mismatch-\(delta)s")
            return
        }
        var payload = basePayload(found: true)
        payload["roundtrip"] = true
        payload["settings"] = fetchedSummary
        payload["epoch_delta_s"] = delta
        finish(code: 0, payload: payload)
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didWriteValueFor characteristic: CBCharacteristic,
        error: Error?
    ) {
        guard !finished, characteristic.uuid == settingsJSONCharacteristicUUID else { return }
        if let error {
            fail(code: 8, error: "settings-write-failed: \(error.localizedDescription)")
            return
        }
        guard phase == .writingEpoch else {
            fail(code: 8, error: "unexpected-settings-write-state")
            return
        }
        phase = .readingVerifiedSettings
        peripheral.readValue(for: characteristic)
    }

    private func basePayload(found: Bool) -> [String: Any] {
        var payload: [String: Any] = [
            "found": found,
            "operation": operation.rawValue,
        ]
        if let peripheral {
            payload["identifier"] = peripheral.identifier.uuidString
        }
        if !advertisedName.isEmpty {
            payload["name"] = advertisedName
            payload["rssi"] = advertisedRSSI
        }
        return payload
    }

    private func fail(code: Int32, error: String) {
        var payload = basePayload(found: false)
        payload["error"] = error
        finish(code: code, payload: payload)
    }

    private func finish(code: Int32, payload: [String: Any]) {
        guard !finished else { return }
        finished = true
        central?.stopScan()
        if let peripheral, peripheral.state != .disconnected {
            central?.cancelPeripheralConnection(peripheral)
        }
        if let data = try? JSONSerialization.data(withJSONObject: payload, options: [.sortedKeys]),
           let line = String(data: data, encoding: .utf8) {
            print(line)
        }
        fflush(stdout)
        exit(code)
    }
}

var needle = "Astrolabe"
var timeout: TimeInterval = 6
var operation = ProbeOperation.advertise
var index = 1
while index < CommandLine.arguments.count {
    switch CommandLine.arguments[index] {
    case "--name-contains" where index + 1 < CommandLine.arguments.count:
        index += 1
        needle = CommandLine.arguments[index]
    case "--timeout" where index + 1 < CommandLine.arguments.count:
        index += 1
        timeout = Double(CommandLine.arguments[index]) ?? timeout
    case "--operation" where index + 1 < CommandLine.arguments.count:
        index += 1
        guard let parsed = ProbeOperation(rawValue: CommandLine.arguments[index]) else {
            fputs("invalid operation: \(CommandLine.arguments[index])\n", stderr)
            exit(64)
        }
        operation = parsed
    case "--help":
        print("usage: lunasay_ble_probe [--name-contains TEXT] [--timeout SECONDS] [--operation advertise|settings-read|settings-roundtrip]")
        exit(0)
    default:
        fputs("unknown argument: \(CommandLine.arguments[index])\n", stderr)
        exit(64)
    }
    index += 1
}

_ = LunaSayProbe(needle: needle, timeout: max(1, timeout), operation: operation)
RunLoop.main.run()
